#pragma once
/* express::App / express::Router: Express on top of Drogon.

     express::App app;
     app.use(express::logger("dev"));
     app.get("/", [](express::Req req, express::Res res) { res.send("hello"); });
     app.get("/users/:id", auth, [](express::Req req, express::Res res) {
         res.json(express::obj("id", req.param("id")));
     });
     app.post("/users", [](express::Req req, express::Res res) -> express::Task<> {     // coroutine handler
         auto doc = co_await User.createAsync(express::nv::body(req));
         res.status(201).json(doc.toJSON());
     });

     express::Router api;
     api.get("/health", [](express::Req, express::Res res) { res.json(express::obj("ok", true)); });
     api.use([](express::Error err, express::Req, express::Res res, express::Next next) {   // error middleware
         res.status(err.status()).json(express::obj("apiError", err.message()));
     });
     app.use("/api", api);                                // -> GET /api/health

     app.route("/books").get(list).post(create);          // one path, many methods
     app.param("id", loadUser);                           // runs before any route with :id
     app.listen(3000);

   A function can be:
     void(Req, Res)                       a handler (ends the request)
     void(Req, Res, Next)                 middleware: call next() to go on, next(err) to fail
     void(Error, Req, Res, Next)          error middleware: runs only when there is an error
     Task<>(...) versions of all three    coroutines (co_await inside)
   Throwing (or co_await-ing something that throws) is the same as next(error).
   throw express::HttpError(404, "User not found") for a ready-made status + JSON body.

   Paths:
     "/users/:id"          param
     "/users/:id?"         optional param
     "/users/:id(\\d+)"    param with a regex constraint
     "/files/ *"           (no space) the last * matches the rest of the path -> req.param("*")
     std::regex("^/v(\\d+)/x$")   a whole regex; groups -> req.param("0"), req.param("1"), ...
   Order matters exactly like Express: layers run in the order they were added.               */

#include <drogon/HttpAppFramework.h>
#include <drogon/utils/coroutine.h>

#include <atomic>
#include <exception>
#include <functional>
#include <map>
#include <memory>
#include <regex>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

#include "context.hpp"
#include "json.hpp"
#include "req.hpp"
#include "res.hpp"

namespace express {

template <class T = void>
using Task = drogon::Task<T>;

// throw from any handler / middleware: becomes `status` + {"error": message}
// extra: more fields for the JSON body, e.g. HttpError(400, "Validation failed", express::obj("errors", e))
class HttpError : public std::runtime_error {
  public:
    HttpError(int status, const std::string &message, Json extra = Json())
        : std::runtime_error(message), status_(status), extra_(std::move(extra)) {}
    int status() const { return status_; }
    const Json &extra() const { return extra_; }
    // response headers to send with the error (e.g. Retry-After, WWW-Authenticate)
    HttpError &header(const std::string &k, const std::string &v) {
        headers_.emplace_back(k, v);
        return *this;
    }
    const std::vector<std::pair<std::string, std::string>> &headers() const { return headers_; }

  private:
    int status_;
    Json extra_;
    std::vector<std::pair<std::string, std::string>> headers_;
};

// The error an error middleware receives: (err, req, res, next) in Express
class Error {
  public:
    explicit Error(std::exception_ptr p) : p_(std::move(p)) {}
    std::string message() const;  // what()
    int status() const;           // HttpError status, otherwise 500
    template <class T>
    bool is() const { return as<T>() != nullptr; }
    template <class T>
    const T *as() const {  // pointer to the exception if it is a T (valid while this Error lives)
        try {
            std::rethrow_exception(p_);
        } catch (const T &e) {
            return &e;
        } catch (...) {
            return nullptr;
        }
    }
    [[noreturn]] void rethrow() const { std::rethrow_exception(p_); }
    const std::exception_ptr &ptr() const { return p_; }

  private:
    std::exception_ptr p_;
};

// next() / next(error). Calling it a second time is ignored (and logged).
class Next {
  public:
    using Fn = std::function<void(std::exception_ptr)>;
    explicit Next(Fn f);
    void operator()() const;
    void operator()(std::exception_ptr err) const;
    void error(const std::string &message, int status = 500) const;
    bool called() const;

  private:
    struct State;
    std::shared_ptr<State> s_;
};

using Middleware = std::function<void(Req, Res, Next)>;
using ErrorMiddleware = std::function<void(Error, Req, Res, Next)>;
using ParamHandler = std::function<void(Req, Res, Next, std::string)>;

namespace detail {

drogon::AsyncTask runTask(std::function<drogon::Task<void>()> make, std::function<void(std::exception_ptr)> done);

template <class F>
constexpr bool isErrorMw = std::is_invocable_v<F &, Error, Req, Res, Next>;

// turn any supported callable into a Middleware
template <class F>
Middleware wrap(F f) {
    if constexpr (std::is_invocable_v<F &, Req, Res, Next>) {
        using R = std::invoke_result_t<F &, Req, Res, Next>;
        if constexpr (std::is_same_v<R, drogon::Task<void>>) {
            return [f = std::move(f)](Req req, Res res, Next next) {
                runTask([f, req, res, next]() mutable { return f(req, res, next); },
                        [next](std::exception_ptr e) { if (e) next(e); });
            };
        } else {
            static_assert(std::is_void_v<R>, "Express++: middleware must return void or express::Task<>");
            return Middleware(std::move(f));
        }
    } else if constexpr (std::is_invocable_v<F &, Req, Res>) {
        using R = std::invoke_result_t<F &, Req, Res>;
        if constexpr (std::is_same_v<R, drogon::Task<void>>) {
            return [f = std::move(f)](Req req, Res res, Next next) {
                runTask([f, req, res]() mutable { return f(req, res); },
                        [next](std::exception_ptr e) { if (e) next(e); });
            };
        } else {
            static_assert(std::is_void_v<R>, "Express++: handler must return void or express::Task<>");
            return [f = std::move(f)](Req req, Res res, Next) mutable { f(req, res); };
        }
    } else {
        static_assert(sizeof(F) == 0,
                      "Express++: expected void(Req, Res), void(Req, Res, Next), void(Error, Req, Res, Next) "
                      "or the Task<> versions");
    }
}

template <class F>
ErrorMiddleware wrapError(F f) {
    using R = std::invoke_result_t<F &, Error, Req, Res, Next>;
    if constexpr (std::is_same_v<R, drogon::Task<void>>) {
        return [f = std::move(f)](Error err, Req req, Res res, Next next) {
            runTask([f, err, req, res, next]() mutable { return f(err, req, res, next); },
                    [next](std::exception_ptr e) { if (e) next(e); });
        };
    } else {
        static_assert(std::is_void_v<R>, "Express++: error middleware must return void or express::Task<>");
        return ErrorMiddleware(std::move(f));
    }
}

template <class F>
ParamHandler wrapParam(F f) {
    using R = std::invoke_result_t<F &, Req, Res, Next, std::string>;
    if constexpr (std::is_same_v<R, drogon::Task<void>>) {
        return [f = std::move(f)](Req req, Res res, Next next, std::string v) {
            runTask([f, req, res, next, v]() mutable { return f(req, res, next, v); },
                    [next](std::exception_ptr e) { if (e) next(e); });
        };
    } else {
        static_assert(std::is_void_v<R>, "Express++: param handler must return void or express::Task<>");
        return ParamHandler(std::move(f));
    }
}

struct Pattern {
    struct Seg {
        enum Kind { Literal, Param, Rest } kind;
        std::string text;
        bool optional = false;
        std::shared_ptr<std::regex> constraint;  // :id(\d+)
    };
    std::vector<Seg> segs;
    std::shared_ptr<std::regex> re;  // whole-path regex route
    std::string source;              // for messages

    static Pattern compile(const std::string &path);
    static Pattern fromRegex(const std::regex &re);
    std::vector<std::string> paramNames() const;
    // end=true: whole path must match; end=false: prefix match.
    // `matched` = the part of the path that matched ("/users/42"), `rest` = what is left ("/posts" or "/")
    bool match(const std::string &path, bool end, std::unordered_map<std::string, std::string> &params,
               std::string &matched, std::string &rest) const;
};

struct RouterImpl;

struct Layer {
    std::string method;  // "" = any (use / all)
    Pattern pattern;
    bool end = true;  // route: exact match; use: prefix match
    Middleware fn;
    ErrorMiddleware errFn;
    std::shared_ptr<RouterImpl> sub;  // mounted router
};

struct RouterImpl {
    std::vector<Layer> layers;
    std::map<std::string, std::vector<ParamHandler>> params;
};

}  // namespace detail

// A path: "/users/:id" style text or a std::regex
struct Path {
    detail::Pattern pattern;
    Path(const char *p) : pattern(detail::Pattern::compile(p)) {}
    Path(const std::string &p) : pattern(detail::Pattern::compile(p)) {}
    Path(std::string_view p) : pattern(detail::Pattern::compile(std::string(p))) {}
    Path(const std::regex &re) : pattern(detail::Pattern::fromRegex(re)) {}
};

class Route;

class Router {
  public:
    Router() : impl_(std::make_shared<detail::RouterImpl>()) {}

    // app.use(mw...)  /  app.use("/path", mw-or-router...)  /  app.use(std::regex(...), mw)
    template <class First, class... Fs>
    Router &use(First &&first, Fs &&...fs) {
        if constexpr (std::is_convertible_v<First, std::string_view> ||
                      std::is_same_v<std::decay_t<First>, std::regex>) {
            static_assert(sizeof...(Fs) > 0, "Express++: use(path, ...) needs at least one middleware or router");
            Path p(first);
            (addUse(p.pattern, std::forward<Fs>(fs)), ...);
        } else {
            auto root = detail::Pattern::compile("/");
            addUse(root, std::forward<First>(first));
            (addUse(root, std::forward<Fs>(fs)), ...);
        }
        return *this;
    }

    template <class... Fs>
    Router &get(Path path, Fs &&...fs) { return addRoute("GET", path, std::forward<Fs>(fs)...); }
    template <class... Fs>
    Router &post(Path path, Fs &&...fs) { return addRoute("POST", path, std::forward<Fs>(fs)...); }
    template <class... Fs>
    Router &put(Path path, Fs &&...fs) { return addRoute("PUT", path, std::forward<Fs>(fs)...); }
    template <class... Fs>
    Router &patch(Path path, Fs &&...fs) { return addRoute("PATCH", path, std::forward<Fs>(fs)...); }
    template <class... Fs>
    Router &del(Path path, Fs &&...fs) { return addRoute("DELETE", path, std::forward<Fs>(fs)...); }
    template <class... Fs>
    Router &options(Path path, Fs &&...fs) { return addRoute("OPTIONS", path, std::forward<Fs>(fs)...); }
    template <class... Fs>
    Router &head(Path path, Fs &&...fs) { return addRoute("HEAD", path, std::forward<Fs>(fs)...); }
    template <class... Fs>
    Router &all(Path path, Fs &&...fs) { return addRoute("", path, std::forward<Fs>(fs)...); }

    // app.route("/books").get(list).post(create).del(remove)
    Route route(Path path);

    // app.param("id", [](Req req, Res res, Next next, std::string id) { ...; next(); })
    // runs once per request, before the first matching layer of this router that has :id
    template <class F>
    Router &param(const std::string &name, F &&fn) {
        impl_->params[name].push_back(detail::wrapParam(std::forward<F>(fn)));
        return *this;
    }

    template <class... Fs>
    Router &addRoute(const std::string &method, const Path &path, Fs &&...fs) {
        static_assert(sizeof...(Fs) > 0, "Express++: a route needs at least one handler");
        (addLayer(method, path.pattern, true, std::forward<Fs>(fs)), ...);
        return *this;
    }

    const std::shared_ptr<detail::RouterImpl> &impl() const { return impl_; }

  private:
    template <class F>
    void addLayer(const std::string &method, const detail::Pattern &p, bool end, F &&f) {
        using D = std::decay_t<F>;
        detail::Layer L;
        L.method = method;
        L.pattern = p;
        L.end = end;
        if constexpr (std::is_base_of_v<Router, D>) {
            L.end = false;
            L.sub = f.impl();
        } else if constexpr (detail::isErrorMw<D>) {
            L.errFn = detail::wrapError(std::forward<F>(f));
        } else {
            L.fn = detail::wrap(std::forward<F>(f));
        }
        impl_->layers.push_back(std::move(L));
    }
    template <class F>
    void addUse(const detail::Pattern &p, F &&f) { addLayer("", p, false, std::forward<F>(f)); }

  protected:
    std::shared_ptr<detail::RouterImpl> impl_;
};

class Route {
  public:
    Route(Router r, Path p) : r_(std::move(r)), p_(std::move(p)) {}
    template <class... Fs>
    Route &get(Fs &&...fs) { r_.addRoute("GET", p_, std::forward<Fs>(fs)...); return *this; }
    template <class... Fs>
    Route &post(Fs &&...fs) { r_.addRoute("POST", p_, std::forward<Fs>(fs)...); return *this; }
    template <class... Fs>
    Route &put(Fs &&...fs) { r_.addRoute("PUT", p_, std::forward<Fs>(fs)...); return *this; }
    template <class... Fs>
    Route &patch(Fs &&...fs) { r_.addRoute("PATCH", p_, std::forward<Fs>(fs)...); return *this; }
    template <class... Fs>
    Route &del(Fs &&...fs) { r_.addRoute("DELETE", p_, std::forward<Fs>(fs)...); return *this; }
    template <class... Fs>
    Route &options(Fs &&...fs) { r_.addRoute("OPTIONS", p_, std::forward<Fs>(fs)...); return *this; }
    template <class... Fs>
    Route &head(Fs &&...fs) { r_.addRoute("HEAD", p_, std::forward<Fs>(fs)...); return *this; }
    template <class... Fs>
    Route &all(Fs &&...fs) { r_.addRoute("", p_, std::forward<Fs>(fs)...); return *this; }

  private:
    Router r_;
    Path p_;
};

inline Route Router::route(Path path) { return Route(*this, std::move(path)); }

class App : public Router {
  public:
    App();

    // Error handlers, tried newest first; return true when handled. These run after all error
    // middleware (router.use((Error, Req, Res, Next))) has passed the error on.
    App &onError(std::function<bool(std::exception_ptr, Req, Res)> fn);
    // Typed shortcut: app.onError<MyError>([](const MyError &e, express::Req, express::Res res) { ... });
    template <class E, class F>
    App &onError(F fn) {
        return onError([fn = std::move(fn)](std::exception_ptr ep, Req req, Res res) mutable {
            try {
                std::rethrow_exception(ep);
            } catch (const E &e) {
                fn(e, req, res);
                return true;
            } catch (...) {
                return false;
            }
        });
    }
    // What to do when no route sent a response (default: 404 "Cannot GET /path")
    App &notFound(std::function<void(Req, Res)> fn);
    // Let requests no Express++ route handled fall through to Drogon (its controllers, static files)
    App &passUnmatchedToDrogon(bool on = true);
    App &threads(size_t n);

    /* Settings (app.set in Express):
         "trust proxy"  true/false   use X-Forwarded-For / -Proto / -Host for req.ip(), protocol(), hostname()
         "views"        "views"      folder for res.render()
         "view engine"  "html"       default template extension (".html")
         "view cache"   true/false   cache templates (default: on in production)
         "body limit"   "10mb"       largest request body Drogon accepts (default 1mb)
         anything else is stored and readable with app.get(key)                                       */
    App &set(const std::string &key, const std::string &value);
    App &set(const std::string &key, const char *value) { return set(key, std::string(value)); }
    App &set(const std::string &key, bool value) { return set(key, std::string(value ? "true" : "false")); }
    App &set(const std::string &key, int value) { return set(key, std::to_string(value)); }
    std::string setting(const std::string &key) const;  // app.get(key) in Express
    App &enable(const std::string &key) { return set(key, true); }
    App &disable(const std::string &key) { return set(key, false); }
    bool enabled(const std::string &key) const { return setting(key) == "true"; }

    // app.locals: data every res.render() sees
    Json &locals();
    // app.engine(".mst", fn): your own template engine for an extension; fn(filePath, data) -> html
    App &engine(const std::string &ext, std::function<std::string(const std::string &, const Json &)> fn);

    // Starts the server and blocks until it stops (express::App::stop() or Ctrl+C)
    void listen(uint16_t port, const std::string &host = "0.0.0.0", std::function<void()> onReady = nullptr);
    static void stop();

    // Install the Express++ router into Drogon without starting it (when you call drogon::app().run() yourself)
    void attach();

    void handleError(std::exception_ptr err, Req req, Res res) const;
    const std::shared_ptr<detail::AppContext> &context() const;

    struct Settings;  // internal

  private:
    std::shared_ptr<Settings> settings_;
};

}  // namespace express
