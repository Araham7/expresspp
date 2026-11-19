#include "express/res.hpp"

#include <drogon/utils/Utilities.h>
#include <trantor/utils/Logger.h>

#include <filesystem>
#include <strings.h>

#include "express/app.hpp"
#include "express/req.hpp"
#include "express/view.hpp"

#include <fstream>
#include <sstream>

namespace express {

namespace {

const char *reasonText(int code) {
    switch (code) {
        case 200: return "OK";
        case 201: return "Created";
        case 202: return "Accepted";
        case 204: return "No Content";
        case 301: return "Moved Permanently";
        case 302: return "Found";
        case 304: return "Not Modified";
        case 400: return "Bad Request";
        case 401: return "Unauthorized";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 409: return "Conflict";
        case 413: return "Payload Too Large";
        case 415: return "Unsupported Media Type";
        case 422: return "Unprocessable Entity";
        case 429: return "Too Many Requests";
        case 500: return "Internal Server Error";
        case 502: return "Bad Gateway";
        case 503: return "Service Unavailable";
        default: return "";
    }
}

bool ieq(const std::string &a, const char *b) { return strcasecmp(a.c_str(), b) == 0; }

// apply status / headers / cookies, run onFinish hooks, hand the response to Drogon on the right loop
void finish(detail::ResState &s, const drogon::HttpResponsePtr &resp, bool applyStatus) {
    if (s.sent.exchange(true)) {
        LOG_WARN << "Express++: response already sent, ignoring a second send";
        return;
    }
    if (applyStatus) resp->setStatusCode(static_cast<drogon::HttpStatusCode>(s.status));
    for (const auto &[k, v] : s.headers) {
        if (ieq(k, "content-type"))
            resp->setContentTypeString(std::string_view(v));
        else
            resp->addHeader(k, v);
    }
    for (const auto &c : s.cookies) resp->addCookie(c);

    FinishInfo info;
    info.status = static_cast<int>(resp->statusCode());
    info.bytes = resp->body().size();
    info.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - s.start).count();
    std::vector<std::function<void(const FinishInfo &)>> hooks;
    {
        std::lock_guard<std::mutex> lk(s.m);
        hooks = s.onFinish;
    }
    for (auto &h : hooks) {
        try {
            h(info);
        } catch (const std::exception &e) {
            LOG_ERROR << "Express++: onFinish hook threw: " << e.what();
        }
    }

    if (s.loop && !s.loop->isInLoopThread())
        s.loop->queueInLoop([send = s.send, resp] { send(resp); });
    else
        s.send(resp);
}

bool hasType(const detail::ResState &s) {
    for (const auto &h : s.headers)
        if (ieq(h.first, "content-type")) return true;
    return false;
}

}  // namespace

Res &Res::set(const std::string &name, const std::string &value) {
    for (auto &h : s_->headers)
        if (strcasecmp(h.first.c_str(), name.c_str()) == 0) {
            h.second = value;
            return *this;
        }
    s_->headers.emplace_back(name, value);
    return *this;
}

Res &Res::cookie(const std::string &name, const std::string &value, const CookieOptions &o) {
    std::string v = value;
    if (o.sign) {
        if (!s_->cookieSigner)
            throw std::runtime_error("Express++: cookieParser(\"secret\") required for signed cookies");
        v = "s:" + s_->cookieSigner(value);
    }
    drogon::Cookie c(name, detail::uriEncode(v));  // encodeURIComponent, like Express
    if (!o.domain.empty()) c.setDomain(o.domain);
    c.setHttpOnly(o.httpOnly);
    c.setSecure(o.secure);
    c.setPath(o.path);
    if (o.maxAge >= 0) c.setMaxAge(o.maxAge);
    if (o.sameSite == "Strict")
        c.setSameSite(drogon::Cookie::SameSite::kStrict);
    else if (o.sameSite == "None")
        c.setSameSite(drogon::Cookie::SameSite::kNone);
    else
        c.setSameSite(drogon::Cookie::SameSite::kLax);
    s_->cookies.push_back(std::move(c));
    return *this;
}

Res &Res::clearCookie(const std::string &name, const std::string &path) {
    drogon::Cookie c(name, "");
    c.setPath(path);
    c.setMaxAge(0);
    c.setExpiresDate(trantor::Date(0));
    s_->cookies.push_back(std::move(c));
    return *this;
}

Res &Res::type(const std::string &t) {
    std::string mime = detail::mimeFor(t);
    if (mime.rfind("text/", 0) == 0 && mime.find("charset") == std::string::npos) mime += "; charset=utf-8";
    return set("Content-Type", mime);
}

Res &Res::append(const std::string &name, const std::string &value) {
    for (auto &h : s_->headers)
        if (strcasecmp(h.first.c_str(), name.c_str()) == 0) {
            h.second += ", " + value;
            return *this;
        }
    s_->headers.emplace_back(name, value);
    return *this;
}

std::string Res::get(const std::string &name) const {
    for (const auto &h : s_->headers)
        if (strcasecmp(h.first.c_str(), name.c_str()) == 0) return h.second;
    return "";
}

Res &Res::vary(const std::string &field) {
    std::string cur = get("Vary");
    if (cur.empty()) return set("Vary", field);
    std::string lc = cur, lf = field;
    for (auto &c : lc) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    for (auto &c : lf) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (lc == "*" || lc.find(lf) != std::string::npos) return *this;
    return set("Vary", cur + ", " + field);
}

Res &Res::attachment(const std::string &filename) {
    if (filename.empty()) return set("Content-Disposition", "attachment");
    std::string base = std::filesystem::path(filename).filename().string();
    std::string safe;
    for (char c : base) safe += (c == '"' || c == '\\' || c == '\r' || c == '\n') ? '_' : c;
    return set("Content-Disposition", "attachment; filename=\"" + safe + "\"");
}

void Res::download(const std::string &path, const std::string &filename) {
    attachment(filename.empty() ? path : filename);
    sendFile(path);
}

std::string Res::renderToString(const std::string &view, const Json &data) const {
    auto app = s_->app ? s_->app : std::make_shared<detail::AppContext>();
    namespace fs = std::filesystem;
    fs::path file = fs::path(app->views) / view;
    if (!file.has_extension()) file += app->viewExt;
    std::string ext = file.extension().string();

    Json merged = app->locals;
    if (!merged.isObject()) merged = Json(::Json::objectValue);
    for (const auto &k : s_->locals.getMemberNames()) merged[k] = s_->locals[k];
    if (data.isObject())
        for (const auto &k : data.getMemberNames()) merged[k] = data[k];

    auto eng = app->engines.find(ext);
    if (eng != app->engines.end()) return eng->second(file.string(), merged);

    bool cache = app->viewCache == 1 || (app->viewCache == -1 && [] {
        for (const char *k : {"EXPRESS_ENV", "NODE_ENV"})
            if (const char *v = std::getenv(k); v && std::string(v) == "production") return true;
        return false;
    }());
    auto load = [app, cache, ext](const fs::path &p) -> std::string {
        std::string key = p.string();
        if (cache) {
            std::lock_guard<std::mutex> lk(app->cacheMutex);
            auto it = app->templateCache.find(key);
            if (it != app->templateCache.end()) return it->second;
        }
        std::ifstream in(p, std::ios::binary);
        if (!in) throw std::runtime_error("Express++: failed to lookup view \"" + key + "\"");
        std::stringstream ss;
        ss << in.rdbuf();
        std::string text = ss.str();
        if (cache) {
            std::lock_guard<std::mutex> lk(app->cacheMutex);
            app->templateCache[key] = text;
        }
        return text;
    };
    std::string tpl = load(file);
    fs::path dir = file.parent_path();
    return renderTemplate(tpl, merged, [&](const std::string &name) {
        fs::path pp = fs::path(app->views) / name;
        if (!pp.has_extension()) pp += ext;
        try {
            return load(pp);
        } catch (...) {
            return std::string();
        }
    });
}

void Res::render(const std::string &view, const Json &data) {
    std::string html = renderToString(view, data);
    if (get("Content-Type").empty()) type("text/html");
    send(std::move(html));
}

void Res::format(const std::vector<std::pair<std::string, std::function<void()>>> &handlers) {
    std::vector<std::string> offered;
    const std::function<void()> *fallback = nullptr;
    for (const auto &h : handlers) {
        if (h.first == "default")
            fallback = &h.second;
        else
            offered.push_back(h.first);
    }
    vary("Accept");
    std::string accept = s_->req ? s_->req->getHeader("accept") : "";
    std::string pick = detail::negotiate(accept, offered);
    if (pick.empty()) {
        if (fallback) {
            (*fallback)();
            return;
        }
        throw HttpError(406, "Not Acceptable");
    }
    if (get("Content-Type").empty()) type(pick);
    for (const auto &h : handlers)
        if (h.first == pick) {
            h.second();
            return;
        }
}

void Res::json(const Json &v) { rawJson(stringify(v)); }

void Res::rawJson(std::string text) {
    auto r = drogon::HttpResponse::newHttpResponse();
    if (!hasType(*s_)) r->setContentTypeCode(drogon::CT_APPLICATION_JSON);
    r->setBody(std::move(text));
    finish(*s_, r, true);
}

void Res::send(std::string body) {
    auto r = drogon::HttpResponse::newHttpResponse();
    if (!hasType(*s_)) r->setContentTypeCode(drogon::CT_TEXT_HTML);
    r->setBody(std::move(body));
    finish(*s_, r, true);
}

void Res::sendStatus(int code) {
    s_->status = code;
    auto r = drogon::HttpResponse::newHttpResponse();
    if (code != 204 && code != 304) {
        if (!hasType(*s_)) r->setContentTypeCode(drogon::CT_TEXT_PLAIN);
        r->setBody(reasonText(code));
    }
    finish(*s_, r, true);
}

void Res::redirect(const std::string &url, int code) {
    s_->status = code;
    auto r = drogon::HttpResponse::newRedirectionResponse(url, static_cast<drogon::HttpStatusCode>(code));
    finish(*s_, r, true);
}

void Res::sendFile(const std::string &path) {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec)) throw HttpError(404, "File not found");
    auto r = drogon::HttpResponse::newFileResponse(path);
    finish(*s_, r, true);
}

void Res::sendRaw(const drogon::HttpResponsePtr &resp) { finish(*s_, resp, false); }

void Res::onFinish(std::function<void(const FinishInfo &)> fn) {
    std::lock_guard<std::mutex> lk(s_->m);
    s_->onFinish.push_back(std::move(fn));
}

}  // namespace express
