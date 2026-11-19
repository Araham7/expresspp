#include "express/app.hpp"

#include <trantor/utils/Logger.h>

#include <cstdlib>
#include <cstring>
#include <strings.h>
#include <thread>

#include "express/body.hpp"

namespace express {

// ---------- Error ----------

std::string Error::message() const {
    try {
        std::rethrow_exception(p_);
    } catch (const std::exception &e) {
        return e.what();
    } catch (...) {
        return "Unknown error";
    }
}

int Error::status() const {
    if (auto *h = as<HttpError>()) return h->status();
    return 500;
}

// ---------- Next ----------

struct Next::State {
    Fn fn;
    std::atomic<bool> called{false};
};

Next::Next(Fn f) : s_(std::make_shared<State>()) { s_->fn = std::move(f); }

void Next::operator()() const { (*this)(std::exception_ptr()); }

void Next::operator()(std::exception_ptr err) const {
    if (s_->called.exchange(true)) {
        if (err) {
            try {
                std::rethrow_exception(err);
            } catch (const std::exception &e) {
                LOG_ERROR << "Express++: error after next() was already called: " << e.what();
            } catch (...) {
                LOG_ERROR << "Express++: unknown error after next() was already called";
            }
        } else {
            LOG_WARN << "Express++: next() called more than once, ignored";
        }
        return;
    }
    s_->fn(err);
}

void Next::error(const std::string &message, int status) const { (*this)(std::make_exception_ptr(HttpError(status, message))); }

bool Next::called() const { return s_->called.load(); }

namespace detail {

// ---------- paths ----------

static std::vector<std::string> splitPath(const std::string &p) {
    std::vector<std::string> out;
    size_t i = 0;
    while (i < p.size()) {
        while (i < p.size() && p[i] == '/') i++;
        size_t j = i;
        int paren = 0;
        while (j < p.size() && (p[j] != '/' || paren > 0)) {  // keep "/" inside :id(...) constraints
            if (p[j] == '(') paren++;
            if (p[j] == ')') paren--;
            j++;
        }
        if (j > i) out.emplace_back(p.substr(i, j - i));
        i = j;
    }
    return out;
}

static std::vector<std::string> splitUrl(const std::string &p) {
    std::vector<std::string> out;
    size_t i = 0;
    while (i < p.size()) {
        while (i < p.size() && p[i] == '/') i++;
        size_t j = p.find('/', i);
        if (j == std::string::npos) j = p.size();
        if (j > i) out.emplace_back(p.substr(i, j - i));
        i = j;
    }
    return out;
}

static std::string joinFrom(const std::vector<std::string> &segs, size_t from, size_t to) {
    std::string s;
    for (size_t i = from; i < to; i++) s += "/" + segs[i];
    return s;
}

Pattern Pattern::compile(const std::string &path) {
    Pattern p;
    p.source = path;
    for (auto &s : splitPath(path)) {
        if (s == "*") {
            p.segs.push_back({Seg::Rest, "*"});
        } else if (s[0] == ':') {
            Seg seg{Seg::Param, ""};
            size_t i = 1;
            while (i < s.size() && (std::isalnum(static_cast<unsigned char>(s[i])) || s[i] == '_')) i++;
            seg.text = s.substr(1, i - 1);
            if (seg.text.empty()) throw std::invalid_argument("Express++: empty parameter name in path '" + path + "'");
            if (i < s.size() && s[i] == '(') {
                size_t close = s.rfind(')');
                if (close == std::string::npos || close < i)
                    throw std::invalid_argument("Express++: unclosed '(' in path '" + path + "'");
                seg.constraint = std::make_shared<std::regex>(s.substr(i + 1, close - i - 1));
                i = close + 1;
            }
            if (i < s.size() && s[i] == '?') {
                seg.optional = true;
                i++;
            }
            if (i != s.size())
                throw std::invalid_argument("Express++: unexpected '" + s.substr(i) + "' in path '" + path + "'");
            p.segs.push_back(std::move(seg));
        } else {
            p.segs.push_back({Seg::Literal, s});
        }
    }
    return p;
}

Pattern Pattern::fromRegex(const std::regex &re) {
    Pattern p;
    p.re = std::make_shared<std::regex>(re);
    p.source = "<regex>";
    return p;
}

std::vector<std::string> Pattern::paramNames() const {
    std::vector<std::string> out;
    for (const auto &s : segs)
        if (s.kind == Seg::Param) out.push_back(s.text);
    return out;
}

bool Pattern::match(const std::string &path, bool end, std::unordered_map<std::string, std::string> &params,
                    std::string &matched, std::string &rest) const {
    if (re) {
        std::smatch m;
        bool ok;
        if (end) {
            ok = std::regex_match(path, m, *re);
        } else {
            ok = std::regex_search(path, m, *re, std::regex_constants::match_continuous);
            if (ok) {  // prefix must stop at a segment boundary
                size_t len = m.length(0);
                ok = len == path.size() || path[len] == '/' || (len > 0 && path[len - 1] == '/');
            }
        }
        if (!ok) return false;
        for (size_t g = 1; g < m.size(); g++) params[std::to_string(g - 1)] = m[g].matched ? m[g].str() : "";
        matched = m.str(0);
        if (!matched.empty() && matched.back() == '/') matched.pop_back();
        rest = path.substr(matched.size());
        if (rest.empty()) rest = "/";
        return true;
    }

    // every layer looks at the same path: split it once per thread and reuse it (no allocations per layer)
    thread_local std::string cachedPath = "\x01";
    thread_local std::vector<std::string> cachedParts;
    if (path != cachedPath) {
        cachedParts = splitUrl(path);
        cachedPath = path;
    }
    const std::vector<std::string> &parts = cachedParts;
    std::unordered_map<std::string, std::string> found;
    size_t pi = 0;  // index into parts
    for (size_t i = 0; i < segs.size(); i++) {
        const auto &ps = segs[i];
        if (ps.kind == Seg::Rest) {
            std::string r = joinFrom(parts, pi, parts.size());
            found["*"] = r.empty() ? "" : r.substr(1);
            matched = path;
            rest = "/";
            for (auto &kv : found) params[kv.first] = kv.second;
            return true;
        }
        if (pi >= parts.size()) {
            if (ps.kind == Seg::Param && ps.optional) continue;  // missing optional param
            return false;
        }
        if (ps.kind == Seg::Literal) {
            if (ps.text != parts[pi]) return false;
        } else {
            if (ps.constraint && !std::regex_match(parts[pi], *ps.constraint)) {
                if (ps.optional) continue;  // optional param that doesn't fit: try the next pattern segment
                return false;
            }
            found[ps.text] = parts[pi];
        }
        pi++;
    }
    if (end && pi != parts.size()) return false;
    matched = joinFrom(parts, 0, pi);
    rest = joinFrom(parts, pi, parts.size());
    if (rest.empty()) rest = "/";
    for (auto &kv : found) params[kv.first] = kv.second;
    return true;
}

// ---------- coroutine launcher ----------

drogon::AsyncTask runTask(std::function<drogon::Task<void>()> make, std::function<void(std::exception_ptr)> done) {
    std::exception_ptr err;
    try {
        co_await make();
    } catch (...) {
        err = std::current_exception();
    }
    done(err);
}

// ---------- the Express-style layer walk ----------

class Dispatch : public std::enable_shared_from_this<Dispatch> {
  public:
    Dispatch(std::shared_ptr<RouterImpl> r, Req q, Res s, std::function<void(std::exception_ptr)> d)
        : router_(std::move(r)), req_(std::move(q)), res_(std::move(s)), done_(std::move(d)) {}

    void next(std::exception_ptr err) {
        auto &layers = router_->layers;
        auto &st = req_.state();
        while (idx_ < layers.size()) {
            size_t li = idx_++;
            const Layer &L = layers[li];
            // with an error only error middleware (and routers, which may hold some) run; without, they are skipped
            if (err ? !(L.errFn || L.sub) : static_cast<bool>(L.errFn)) continue;
            if (!L.method.empty()) {
                const char *m = req_.raw()->methodString();  // no std::string per layer
                if (L.method != m && !(std::strcmp(m, "HEAD") == 0 && L.method == "GET")) continue;
            }
            std::unordered_map<std::string, std::string> found;
            std::string matched, rest;
            if (!L.pattern.match(st.path, L.end, found, matched, rest)) continue;
            for (auto &kv : found) st.params[kv.first] = kv.second;

            auto self = shared_from_this();
            auto run = [self, li, err, matched, rest] { self->invoke(li, err, matched, rest); };
            if (!err && !router_->params.empty())
                runParams(L.pattern.paramNames(), 0, run);
            else
                run();
            return;
        }
        done_(err);
    }

  private:
    void invoke(size_t li, std::exception_ptr err, const std::string &matched, const std::string &rest) {
        const Layer &L = router_->layers[li];
        auto &st = req_.state();
        if (L.sub) {
            // enter the mounted router with the prefix stripped; restore on the way out
            std::string savedPath = st.path, savedBase = st.baseUrl;
            st.path = rest;
            st.baseUrl += matched;
            auto self = shared_from_this();
            auto child = std::make_shared<Dispatch>(L.sub, req_, res_, [self, savedPath, savedBase](std::exception_ptr e) {
                auto &s = self->req_.state();
                s.path = savedPath;
                s.baseUrl = savedBase;
                self->next(e);
            });
            child->next(err);
            return;
        }
        auto self = shared_from_this();
        // app.use("/pub", mw): inside mw, req.path() is relative to "/pub" and req.baseUrl() is "/pub"
        // (like Express); both are put back when mw calls next()
        bool mounted = !L.end && matched != "" && matched != "/";
        std::string savedPath = st.path, savedBase = st.baseUrl;
        if (mounted) {
            st.path = rest;
            st.baseUrl += matched;
        }
        Next nx([self, mounted, savedPath, savedBase](std::exception_ptr e) {
            if (mounted) {
                auto &s = self->req_.state();
                s.path = savedPath;
                s.baseUrl = savedBase;
            }
            self->next(e);
        });
        try {
            if (err)
                L.errFn(Error(err), req_, res_, nx);
            else
                L.fn(req_, res_, nx);
        } catch (...) {
            nx(std::current_exception());
        }
    }

    // app.param() callbacks for the params this layer matched, one after another, then `then`
    void runParams(std::vector<std::string> names, size_t i, std::function<void()> then) {
        auto &st = req_.state();
        for (; i < names.size(); i++) {
            auto it = router_->params.find(names[i]);
            if (it == router_->params.end()) continue;
            auto pv = st.params.find(names[i]);
            if (pv == st.params.end()) continue;  // optional param not present
            std::string key = std::to_string(reinterpret_cast<uintptr_t>(router_.get())) + "|" + names[i] + "=" + pv->second;
            if (!st.paramsCalled.insert(key).second) continue;
            runParamFns(&it->second, 0, pv->second, [self = shared_from_this(), names, i, then] {
                self->runParams(names, i + 1, then);
            });
            return;
        }
        then();
    }

    void runParamFns(const std::vector<ParamHandler> *fns, size_t k, std::string value, std::function<void()> then) {
        if (k >= fns->size()) {
            then();
            return;
        }
        auto self = shared_from_this();
        Next nx([self, fns, k, value, then](std::exception_ptr e) {
            if (e)
                self->next(e);
            else
                self->runParamFns(fns, k + 1, value, then);
        });
        try {
            (*fns)[k](req_, res_, nx, value);
        } catch (...) {
            nx(std::current_exception());
        }
    }

    std::shared_ptr<RouterImpl> router_;
    Req req_;
    Res res_;
    std::function<void(std::exception_ptr)> done_;
    size_t idx_ = 0;
};

}  // namespace detail

// ---------- App ----------

struct App::Settings {
    std::vector<std::function<bool(std::exception_ptr, Req, Res)>> errorHandlers;
    std::function<void(Req, Res)> notFound;
    bool passToDrogon = false;
    size_t threads = 0;
    bool attached = false;
    std::shared_ptr<detail::AppContext> ctx = std::make_shared<detail::AppContext>();
};

static bool isProduction() {
    for (const char *k : {"EXPRESS_ENV", "NODE_ENV"})
        if (const char *v = std::getenv(k); v && std::string(v) == "production") return true;
    return false;
}

static void handleErrorImpl(const App::Settings &s, std::exception_ptr err, Req req, Res res) {
    if (res.sent()) {
        try {
            std::rethrow_exception(err);
        } catch (const std::exception &e) {
            LOG_ERROR << "Express++: error after the response was sent: " << e.what();
        } catch (...) {
        }
        return;
    }
    for (auto it = s.errorHandlers.rbegin(); it != s.errorHandlers.rend(); ++it) {
        try {
            if ((*it)(err, req, res)) return;
        } catch (const std::exception &e) {
            LOG_ERROR << "Express++: error handler threw: " << e.what();
        }
    }
    try {
        std::rethrow_exception(err);
    } catch (const HttpError &e) {
        Json body = obj("error", e.what());
        if (e.extra().isObject())
            for (const auto &k : e.extra().getMemberNames()) body[k] = e.extra()[k];
        for (const auto &[k, v] : e.headers()) res.set(k, v);
        res.status(e.status()).json(body);
    } catch (const std::exception &e) {
        LOG_ERROR << "Express++: " << req.method() << " " << req.originalUrl() << ": " << e.what();
        res.status(500).json(obj("error", isProduction() ? "Internal Server Error" : e.what()));
    } catch (...) {
        LOG_ERROR << "Express++: " << req.method() << " " << req.originalUrl() << ": unknown exception";
        res.status(500).json(obj("error", "Internal Server Error"));
    }
}

App::App() : settings_(std::make_shared<Settings>()) {}

App &App::onError(std::function<bool(std::exception_ptr, Req, Res)> fn) {
    settings_->errorHandlers.push_back(std::move(fn));
    return *this;
}
App &App::notFound(std::function<void(Req, Res)> fn) {
    settings_->notFound = std::move(fn);
    return *this;
}
App &App::passUnmatchedToDrogon(bool on) {
    settings_->passToDrogon = on;
    return *this;
}
App &App::threads(size_t n) {
    settings_->threads = n;
    return *this;
}

App &App::set(const std::string &key, const std::string &value) {
    auto &c = *settings_->ctx;
    auto isTrue = [&] { return value == "true" || value == "1" || value == "yes" || value == "on"; };
    if (key == "trust proxy")
        c.trustProxy = isTrue();
    else if (key == "views")
        c.views = value;
    else if (key == "view engine")
        c.viewExt = value.empty() || value[0] == '.' ? value : "." + value;
    else if (key == "view cache")
        c.viewCache = isTrue() ? 1 : 0;
    else if (key == "body limit")
        c.bodyLimit = parseBytes(value);
    c.settings[key] = value;
    return *this;
}

std::string App::setting(const std::string &key) const {
    auto it = settings_->ctx->settings.find(key);
    return it == settings_->ctx->settings.end() ? "" : it->second;
}

Json &App::locals() { return settings_->ctx->locals; }

App &App::engine(const std::string &ext, std::function<std::string(const std::string &, const Json &)> fn) {
    settings_->ctx->engines[ext.empty() || ext[0] == '.' ? ext : "." + ext] = std::move(fn);
    return *this;
}

const std::shared_ptr<detail::AppContext> &App::context() const { return settings_->ctx; }

void App::handleError(std::exception_ptr err, Req req, Res res) const { handleErrorImpl(*settings_, err, req, res); }

void App::attach() {
    if (settings_->attached) return;
    settings_->attached = true;
    auto impl = impl_;
    auto s = settings_;
    drogon::app().registerPreRoutingAdvice([impl, s](const drogon::HttpRequestPtr &raw, drogon::AdviceCallback &&acb,
                                                     drogon::AdviceChainCallback &&chain) {
        // WebSocket upgrades belong to Drogon's WebSocket controllers (socketiopp, your own WS_PATH_ADD classes),
        // just like Node's Express never sees "upgrade" requests
        if (strcasecmp(raw->getHeader("upgrade").c_str(), "websocket") == 0) {
            chain();
            return;
        }
        auto rs = std::make_shared<detail::ReqState>();
        rs->raw = raw;
        rs->app = s->ctx;
        rs->path = raw->path();
        auto ss = std::make_shared<detail::ResState>();
        ss->send = std::move(acb);
        ss->req = raw;
        ss->app = s->ctx;
        ss->loop = trantor::EventLoop::getEventLoopOfCurrentThread();
        Req req(rs);
        Res res(ss);
        auto d = std::make_shared<detail::Dispatch>(
            impl, req, res, [s, req, res, chain = std::move(chain)](std::exception_ptr err) mutable {
                if (err) {
                    handleErrorImpl(*s, err, req, res);
                    return;
                }
                if (res.sent()) return;
                if (s->passToDrogon) {
                    chain();
                } else if (s->notFound) {
                    try {
                        s->notFound(req, res);
                    } catch (...) {
                        handleErrorImpl(*s, std::current_exception(), req, res);
                    }
                } else {
                    res.status(404).type("text/plain").send("Cannot " + req.method() + " " + req.originalUrl());
                }
            });
        d->next(nullptr);
    });
}

void App::listen(uint16_t port, const std::string &host, std::function<void()> onReady) {
    attach();
    size_t n = settings_->threads ? settings_->threads : std::max(1u, std::thread::hardware_concurrency());
    auto &a = drogon::app();
    a.addListener(host, port).setThreadNum(n);
    if (settings_->ctx->bodyLimit) {
        a.setClientMaxBodySize(settings_->ctx->bodyLimit);
        a.setClientMaxMemoryBodySize(std::min<size_t>(settings_->ctx->bodyLimit, 64 * 1024));
    }
    if (setting("x-powered-by") == "false") a.enableServerHeader(false);  // app.disable("x-powered-by")
    if (onReady) a.registerBeginningAdvice(std::move(onReady));
    a.run();
}

void App::stop() { drogon::app().quit(); }

}  // namespace express
