#pragma once
/* express::Req: the request, Express style. A cheap handle (shared state), so pass it by value and
   keep it inside lambdas / coroutines without worrying about lifetime.

     req.method()            "GET"
     req.path()              path inside the current router ("/42" under app.use("/users", r))
     req.originalUrl()       full path as sent ("/users/42")
     req.baseUrl()           where the current router is mounted ("/users")
     req.param("id")         route parameter from "/users/:id"
     req.query("page", "1")  query string (and urlencoded form) value, with a default
     req.header("x-token")   request header (case-insensitive)
     req.body()              raw body text
     req.json()              body parsed as JSON (null if it isn't JSON)
     req.cookie("sid")       cookie value (URL-decoded, like Express)
     req.cookies()           all cookies {name: value} (signed ones excluded once cookieParser runs)
     req.signedCookie("uid") verified signed cookie (std::nullopt if missing or tampered)
     req.signedCookies()     all valid signed cookies (needs express::cookieParser("secret"))
     req.ip() / req.ips()    client address (X-Forwarded-For when "trust proxy" is on)
     req.hostname()          "example.com" (Host / X-Forwarded-Host)
     req.protocol()          "http" | "https"     req.secure()  protocol() == "https"
     req.xhr()               X-Requested-With: XMLHttpRequest
     req.accepts("json", "html")   best type the client accepts ("" = none)
     req.is("json")          does the body's Content-Type match?
     req.set("user", u) / req.get<User>("user") / req.has("user")   data for later middleware
     req.raw()               the underlying drogon::HttpRequestPtr                                */

#include <drogon/HttpRequest.h>

#include <any>
#include <memory>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include "context.hpp"
#include "json.hpp"

namespace express {

namespace detail {
struct ReqState {
    drogon::HttpRequestPtr raw;
    std::shared_ptr<AppContext> app;
    std::string path;     // relative to the current router
    std::string baseUrl;  // prefix eaten by mounted routers
    std::unordered_map<std::string, std::string> params;
    std::unordered_map<std::string, std::any> locals;
    std::set<std::string> paramsCalled;  // app.param() callbacks already run
    bool cookieParsed = false;           // express::cookieParser ran
    std::unordered_map<std::string, std::string> signedCookies;  // valid ones
    std::set<std::string> signedNames;   // every "s:" cookie seen (valid or not)
    std::vector<std::string> cookieSecrets;
};

// encodeURIComponent / decodeURIComponent (decode returns the input unchanged if it is malformed)
std::string uriEncode(const std::string &s);
std::string uriDecode(const std::string &s);

// content negotiation helpers (shared with Res::format)
std::string mimeFor(const std::string &typeOrShorthand);  // "json" -> "application/json"
// best of `offered` for an Accept header; returns the offered entry as written, or "" if none
std::string negotiate(const std::string &accept, const std::vector<std::string> &offered);
bool typeIs(const std::string &contentType, const std::string &typeOrShorthand);
}  // namespace detail

class Req {
  public:
    explicit Req(std::shared_ptr<detail::ReqState> s) : s_(std::move(s)) {}

    std::string method() const { return s_->raw->methodString(); }
    const std::string &path() const { return s_->path; }
    const std::string &baseUrl() const { return s_->baseUrl; }
    std::string originalUrl() const { return s_->raw->path(); }

    std::string param(const std::string &name, const std::string &def = "") const {
        auto it = s_->params.find(name);
        return it == s_->params.end() ? def : it->second;
    }
    const std::unordered_map<std::string, std::string> &params() const { return s_->params; }

    std::string query(const std::string &name, const std::string &def = "") const {
        const auto &all = s_->raw->getParameters();
        auto it = all.find(name);
        return it == all.end() ? def : it->second;
    }
    const auto &queries() const { return s_->raw->getParameters(); }

    const std::string &header(const std::string &name) const { return s_->raw->getHeader(name); }
    std::string cookie(const std::string &name) const { return detail::uriDecode(s_->raw->getCookie(name)); }
    std::unordered_map<std::string, std::string> cookies() const;
    std::optional<std::string> signedCookie(const std::string &name) const {
        auto it = s_->signedCookies.find(name);
        if (it == s_->signedCookies.end()) return std::nullopt;
        return it->second;
    }
    const std::unordered_map<std::string, std::string> &signedCookies() const { return s_->signedCookies; }
    // the first cookieParser secret ("" without one), like req.secret
    std::string secret() const { return s_->cookieSecrets.empty() ? "" : s_->cookieSecrets.front(); }

    std::string ip() const;
    std::vector<std::string> ips() const;  // X-Forwarded-For chain (trust proxy only)
    std::string hostname() const;
    std::string protocol() const;
    bool secure() const { return protocol() == "https"; }
    bool xhr() const;

    // req.accepts("json", "html") -> "json" / "html" / ""
    template <class... T>
    std::string accepts(T &&...types) const {
        return detail::negotiate(header("accept"), {std::string(std::forward<T>(types))...});
    }
    std::string accepts(const std::vector<std::string> &types) const { return detail::negotiate(header("accept"), types); }
    bool is(const std::string &type) const;

    std::string body() const { return std::string(s_->raw->body()); }
    const Json &json() const {
        static const Json null;
        auto p = s_->raw->getJsonObject();
        return p ? *p : null;
    }

    // request-scoped data (req.user in Express)
    template <class T>
    void set(const std::string &key, T value) { s_->locals[key] = std::move(value); }
    bool has(const std::string &key) const { return s_->locals.count(key) > 0; }
    template <class T>
    T &get(const std::string &key) const {
        auto it = s_->locals.find(key);
        if (it == s_->locals.end()) throw std::out_of_range("Express++: req.get: no value for '" + key + "'");
        return std::any_cast<T &>(it->second);
    }

    const drogon::HttpRequestPtr &raw() const { return s_->raw; }
    detail::ReqState &state() const { return *s_; }

  private:
    std::shared_ptr<detail::ReqState> s_;
};

}  // namespace express
