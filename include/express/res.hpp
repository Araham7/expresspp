#pragma once
/* express::Res: the response, Express style (cheap handle, pass by value).

     res.status(201).json(express::obj("id", 7));
     res.json(user);                       // nevla::Value / document: anything with .toJson()
     res.send("hello");                    // text/html like Express
     res.type("text/plain").send("ok");
     res.set("X-Version", "1");
     res.cookie("sid", "abc", {.httpOnly = true, .maxAge = 3600});
     res.cookie("uid", "42", {.sign = true});   // signed (needs app.use(express::cookieParser("secret")))
     res.redirect("/login");
     res.sendStatus(204);
     res.sendFile("public/logo.png");
     res.onFinish([](const express::FinishInfo &i) { ... });   // status, bytes, ms (used by express::logger)
     res.append("Link", "<a>"); res.location("/x"); res.vary("Accept");
     res.attachment("report.csv").send(csv);   // Content-Disposition: attachment
     res.download("files/r.pdf", "Report.pdf");
     res.render("profile", express::obj("name", "Asha"));   // views/profile.html (mustache)
     res.locals()["user"] = "asha";                         // data for render()
     res.format({{"html", [&] { res.send("<b>hi</b>"); }}, {"json", [&] { res.json(...); }}});

   It is safe to send from any thread: the response is handed back to the request's own IO loop.
   Sending twice is ignored (and logged), like "headers already sent" in Express.                 */

#include <drogon/HttpResponse.h>
#include <trantor/net/EventLoop.h>

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "context.hpp"
#include "json.hpp"

#include <drogon/HttpRequest.h>

namespace express {

struct FinishInfo {
    int status = 0;
    size_t bytes = 0;
    double ms = 0;  // time since the request arrived
};

struct CookieOptions {
    bool httpOnly = true;
    bool secure = false;
    int maxAge = -1;  // seconds, -1 = session cookie
    std::string path = "/";
    std::string sameSite = "Lax";  // "Lax" | "Strict" | "None"
    bool sign = false;             // signed cookie (Express's {signed: true}); needs express::cookieParser("secret")
    std::string domain;
};

namespace detail {
struct ResState {
    std::function<void(const drogon::HttpResponsePtr &)> send;  // Drogon's callback
    drogon::HttpRequestPtr req;                                 // for format() / render()
    std::shared_ptr<AppContext> app;
    Json locals;  // null until used (an empty object costs an allocation per request)
    trantor::EventLoop *loop = nullptr;                         // request's IO loop
    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    int status = 200;
    std::vector<std::pair<std::string, std::string>> headers;
    std::vector<drogon::Cookie> cookies;
    std::atomic<bool> sent{false};
    std::mutex m;
    std::vector<std::function<void(const FinishInfo &)>> onFinish;
    std::function<std::string(const std::string &)> cookieSigner;  // set by express::cookieParser(secret)
};
}  // namespace detail

class Res {
  public:
    explicit Res(std::shared_ptr<detail::ResState> s) : s_(std::move(s)) {}

    Res &status(int code) { s_->status = code; return *this; }
    int statusCode() const { return s_->status; }
    Res &set(const std::string &name, const std::string &value);
    Res &type(const std::string &contentType);  // "json" / "html" / "text/csv" ...
    Res &append(const std::string &name, const std::string &value);  // add to a header (comma-joined)
    std::string get(const std::string &name) const;                  // a header set so far
    Res &location(const std::string &url) { return set("Location", url); }
    Res &vary(const std::string &field);
    Res &attachment(const std::string &filename = "");
    Res &cookie(const std::string &name, const std::string &value, const CookieOptions &o = {});
    Res &clearCookie(const std::string &name, const std::string &path = "/");

    void json(const Json &v);
    template <class T>
        requires HasToJson<T> && (!std::same_as<std::decay_t<T>, Json>)
    void json(const T &v) { rawJson(std::string(v.toJson())); }
    void rawJson(std::string text);  // already-serialized JSON
    void send(std::string body);      // text/html unless a type was set
    void sendStatus(int code);        // e.g. 204, or 404 with body "Not Found"
    void redirect(const std::string &url, int code = 302);
    void sendFile(const std::string &path);
    void download(const std::string &path, const std::string &filename = "");
    // views/<view><ext> rendered with app.locals + res.locals + data (mustache, or app.engine())
    void render(const std::string &view, const Json &data = Json(::Json::objectValue));
    std::string renderToString(const std::string &view, const Json &data = Json(::Json::objectValue)) const;
    // content negotiation: runs the handler for the best Accept match ("default" = fallback), else 406
    void format(const std::vector<std::pair<std::string, std::function<void()>>> &handlers);
    Json &locals() {
        if (s_->locals.isNull()) s_->locals = Json(::Json::objectValue);
        return s_->locals;
    }
    void end() { sendStatus(s_->status); }

    bool sent() const { return s_->sent.load(); }
    void onFinish(std::function<void(const FinishInfo &)> fn);

    // build your own drogon response and still get headers/cookies/onFinish applied
    void sendRaw(const drogon::HttpResponsePtr &resp);
    detail::ResState &state() const { return *s_; }

  private:
    std::shared_ptr<detail::ResState> s_;
};

}  // namespace express
