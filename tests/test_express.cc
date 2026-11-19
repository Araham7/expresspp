/* Express++ self-test: starts a real server, sends real HTTP requests, checks the answers.
   Exit code 0 = all good.                                                                          */
#include <express/express.hpp>
#include <drogon/HttpClient.h>
#ifdef EXPRESSPP_TEST_NEVLA
#include <express/nevla.hpp>
#endif

#include <algorithm>
#include <atomic>
#include <iterator>
#include <drogon/UploadFile.h>
#include <drogon/WebSocketClient.h>
#include <drogon/WebSocketController.h>
#include <future>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <set>
#include <thread>
#include <vector>

using namespace drogon;

static int gPass = 0, gFail = 0;
#define CHECK(cond, name)                                                  \
    do {                                                                   \
        if (cond) {                                                        \
            gPass++;                                                       \
        } else {                                                           \
            gFail++;                                                       \
            std::cout << "  FAIL: " << name << "  (" << #cond << ")\n";    \
            dumpLast();                                                    \
        }                                                                  \
    } while (0)

static const int PORT = 18766;
static drogon::HttpResponsePtr gLast;
static void dumpLast() {
    if (!gLast) return;
    std::cout << "        last response: " << gLast->statusCode() << " ";
    for (auto &[k, v] : gLast->headers()) std::cout << k << "=" << v << "; ";
    std::cout << "body=" << std::string(gLast->body()).substr(0, 120) << "\n";
}
static HttpClientPtr gClient;

struct Resp {
    int status = 0;
    std::string body;
    HttpResponsePtr raw;
    express::Json json() const {
        try {
            return express::parseJson(body);
        } catch (...) {
            return {};
        }
    }
    std::string header(const std::string &h) const { return raw ? raw->getHeader(h) : ""; }
};

static Resp call(HttpMethod m, const std::string &path, const std::string &body = "",
                 std::vector<std::pair<std::string, std::string>> headers = {}) {
    auto req = HttpRequest::newHttpRequest();
    req->setMethod(m);
    req->setPath(path);
    for (auto &[k, v] : headers) req->addHeader(k, v);
    if (!body.empty()) {
        req->setContentTypeCode(CT_APPLICATION_JSON);
        req->setBody(body);
    }
    auto [r, resp] = gClient->sendRequest(req, 10.0);
    Resp out;
    if (r == ReqResult::Ok && resp) {
        out.status = resp->statusCode();
        out.body = std::string(resp->body());
        out.raw = resp;
    }
    gLast = resp;
    return out;
}

static std::atomic<int> gLogLines{0};
static std::atomic<int> gWrongThread{0};
static std::atomic<int> gParamCalls{0};

#ifdef EXPRESSPP_TEST_NEVLA
static nevla::Model Users() { return nevla::model("User"); }
#endif

static void buildApp(express::App &app) {
    app.use(express::logger("tiny", [](const std::string &) { gLogLines++; }));
    app.use(express::cors({.origins = {"http://good.example"}, .credentials = true, .maxAge = 600}));

    // order of middleware + request-scoped data
    app.use([](express::Req req, express::Res, express::Next next) {
        req.set<std::string>("trace", "a");
        next();
    });
    app.use([](express::Req req, express::Res, express::Next next) {
        req.get<std::string>("trace") += ",b";
        next();
    });
    // coroutine middleware
    app.use([](express::Req req, express::Res, express::Next next) -> express::Task<> {
        req.get<std::string>("trace") += ",co";
        next();
        co_return;
    });

    app.get("/", [](express::Req, express::Res res) { res.send("hello"); });
    app.get("/users/:id", [](express::Req req, express::Res res) {
        res.json(express::obj("id", req.param("id"), "page", req.query("page", "1")));
    });
    app.post("/echo", [](express::Req req, express::Res res) { res.json(req.json()); });
    app.get("/trace", [](express::Req req, express::Res res) { res.json(express::obj("trace", req.get<std::string>("trace"))); });
    app.get("/files/*", [](express::Req req, express::Res res) { res.json(express::obj("rest", req.param("*"))); });

    // errors
    app.get("/teapot", [](express::Req, express::Res) { throw express::HttpError(418, "I'm a teapot"); });
    app.get("/boom", [](express::Req, express::Res) { throw std::runtime_error("kaboom"); });
    app.get("/denied", [](express::Req, express::Res, express::Next next) { next.error("nope", 401); });
    app.get("/coboom", [](express::Req, express::Res) -> express::Task<> {
        co_await drogon::sleepCoro(trantor::EventLoop::getEventLoopOfCurrentThread(), 0.001);
        throw express::HttpError(422, "late failure");
    });

    // route-level middleware
    auto auth = [](express::Req req, express::Res, express::Next next) {
        if (req.header("authorization") != "Bearer secret") throw express::HttpError(401, "Unauthorized");
        req.set<std::string>("user", "asha");
        next();
    };
    app.get("/private", auth, [](express::Req req, express::Res res) { res.json(express::obj("user", req.get<std::string>("user"))); });

    // response helpers
    app.get("/nothing", [](express::Req, express::Res res) { res.sendStatus(204); });
    app.get("/go", [](express::Req, express::Res res) { res.redirect("/elsewhere"); });

    // cookie-parser: signed cookies, secret rotation
    express::Router cp;
    cp.use(express::cookieParser({"new-secret", "old-secret"}));
    cp.get("/set", [](express::Req, express::Res res) {
        res.cookie("uid", "42", {.sign = true}).cookie("theme", "dark mode").send("ok");
    });
    cp.get("/read", [](express::Req req, express::Res res) {
        auto plain = req.cookies();
        auto uid = req.signedCookie("uid");
        auto note = req.signedCookie("note");
        res.json(express::obj("uid", uid.value_or("<none>"), "note", note.value_or("<none>"), "theme",
                              plain.count("theme") ? plain["theme"] : "<none>", "uidInPlain", plain.count("uid") > 0,
                              "signedCount", static_cast<int>(req.signedCookies().size()), "secret", req.secret()));
    });
    app.use("/cp", cp);
    app.get("/unsigned-sign", [](express::Req, express::Res res) { res.cookie("x", "1", {.sign = true}).send("no"); });
    app.get("/plain-cookies", [](express::Req req, express::Res res) {
        auto all = req.cookies();
        res.json(express::obj("a", all["a"], "n", static_cast<int>(all.size()), "one", req.cookie("a")));
    });
    app.get("/cookie", [](express::Req, express::Res res) {
        res.cookie("sid", "abc", {.maxAge = 60}).set("X-Express", "1").type("text/plain").send("ok");
    });
    app.get("/twice", [](express::Req, express::Res res) {
        res.send("first");
        res.send("second");  // ignored
    });

    // async send from another thread
    app.get("/thread", [](express::Req, express::Res res) {
        std::thread([res]() mutable { res.json(express::obj("from", "thread")); }).detach();
    });

    // mounted router with params on both levels
    express::Router api;
    api.use([](express::Req req, express::Res, express::Next next) {
        req.set<std::string>("router", "api");
        next();
    });
    api.get("/items/:item", [](express::Req req, express::Res res) {
        res.json(express::obj("ver", req.param("ver"), "item", req.param("item"), "baseUrl", req.baseUrl(),
                          "path", req.path(), "router", req.get<std::string>("router")));
    });
    express::Router inner;
    inner.get("/", [](express::Req req, express::Res res) { res.json(express::obj("inner", true, "baseUrl", req.baseUrl())); });
    api.use("/deep", inner);
    app.use("/api/:ver", api);
    app.get("/after-api", [](express::Req req, express::Res res) {
        res.json(express::obj("path", req.path(), "baseUrl", req.baseUrl(), "hasRouter", req.has("router")));
    });


    // ======================= v0.2 =======================
    app.set("trust proxy", true);
    app.set("views", "views_test");
    app.locals()["site"] = "MySite";
    app.engine(".txt", [](const std::string &path, const express::Json &data) {
        return "ENGINE:" + data["name"].asString() + ":" + std::to_string(path.size() > 0);
    });

    // app.route() chaining
    app.route("/books")
        .get([](express::Req, express::Res res) { res.send("list"); })
        .post([](express::Req, express::Res res) { res.status(201).send("create"); })
        .del([](express::Req, express::Res res) { res.send("remove"); });

    // optional param, constraint, regex path
    app.get("/opt/:a/:b?", [](express::Req req, express::Res res) {
        res.json(express::obj("a", req.param("a"), "b", req.param("b", "none")));
    });
    app.get("/num/:id(\\d+)", [](express::Req req, express::Res res) { res.json(express::obj("num", req.param("id"))); });
    app.get("/num/:slug", [](express::Req req, express::Res res) { res.json(express::obj("slug", req.param("slug"))); });
    app.get(std::regex("^/v(\\d+)/status$"), [](express::Req req, express::Res res) {
        res.json(express::obj("version", req.param("0")));
    });

    // app.param
    app.param("uid", [](express::Req req, express::Res, express::Next next, std::string uid) {
        gParamCalls++;
        if (uid == "bad") throw express::HttpError(404, "no such user");
        req.set<std::string>("user", "user-" + uid);
        next();
    });
    app.get("/u/:uid", [](express::Req, express::Res, express::Next next) { next(); },
            [](express::Req req, express::Res res) { res.json(express::obj("user", req.get<std::string>("user"))); });

    // router-level error middleware
    express::Router errApi;
    errApi.get("/fail", [](express::Req, express::Res) { throw std::runtime_error("api broke"); });
    errApi.get("/conflict", [](express::Req, express::Res) { throw express::HttpError(409, "conflict"); });
    errApi.get("/late", [](express::Req, express::Res) -> express::Task<> {
        co_await drogon::sleepCoro(trantor::EventLoop::getEventLoopOfCurrentThread(), 0.001);
        throw std::runtime_error("late api error");
    });
    errApi.use([](express::Error err, express::Req, express::Res, express::Next next) {
        if (err.is<express::HttpError>()) return next(err.ptr());  // pass HttpErrors on to the app
        next(err.ptr());
    });
    errApi.use([](express::Error err, express::Req req, express::Res res, express::Next) -> express::Task<> {
        co_await drogon::sleepCoro(trantor::EventLoop::getEventLoopOfCurrentThread(), 0.001);
        if (err.is<express::HttpError>()) throw express::HttpError(err.status(), "rethrown: " + err.message());
        res.status(503).json(express::obj("apiError", err.message(), "baseUrl", req.baseUrl()));
    });
    app.use("/errapi", errApi);

    express::Router rec;  // an error handler that recovers with next()
    rec.get("/x", [](express::Req, express::Res) { throw std::runtime_error("oops"); });
    rec.use([](express::Error, express::Req req, express::Res, express::Next next) {
        req.set<bool>("recovered", true);
        next();
    });
    rec.get("/x", [](express::Req req, express::Res res) { res.json(express::obj("recovered", req.get<bool>("recovered"))); });
    app.use("/rec", rec);

    // response additions
    app.get("/dl", [](express::Req, express::Res res) { res.download("public_test/a.txt", "Report.txt"); });
    app.get("/attach", [](express::Req, express::Res res) { res.attachment("data.csv").type("csv").send("a,b"); });
    app.get("/render", [](express::Req, express::Res res) {
        res.locals()["who"] = "locals";
        res.render("page", express::obj("name", "<Asha>", "raw", "<b>x</b>", "items", express::arr("x", "y"),
                                        "user", express::obj("city", "Durgapur")));
    });
    app.get("/render-engine", [](express::Req, express::Res res) { res.render("thing.txt", express::obj("name", "eng")); });
    app.get("/render-missing", [](express::Req, express::Res res) { res.render("nope"); });
    app.get("/fmt", [](express::Req, express::Res res) {
        res.format({{"text/html", [res]() mutable { res.send("<p>hi</p>"); }},
                    {"json", [res]() mutable { res.json(express::obj("hi", true)); }}});
    });
    app.get("/fmt-default", [](express::Req, express::Res res) {
        res.format({{"json", [res]() mutable { res.json(express::obj("hi", true)); }},
                    {"default", [res]() mutable { res.type("text").send("fallback"); }}});
    });
    app.get("/headers", [](express::Req, express::Res res) {
        res.append("X-List", "a").append("X-List", "b").vary("Accept").vary("Origin").vary("accept");
        res.location("/there").status(201).send("ok");
    });

    // request additions
    app.post("/reqinfo", [](express::Req req, express::Res res) {
        res.json(express::obj("hostname", req.hostname(), "protocol", req.protocol(), "secure", req.secure(),
                              "ip", req.ip(), "xhr", req.xhr(), "accepts", req.accepts("json", "html"),
                              "isJson", req.is("json"), "isHtml", req.is("html")));
    });

    // static files
    app.use("/pub", express::serveStatic("public_test", {.maxAge = 60, .extensions = {".html"}}));
    app.use("/spa", express::serveStatic("public_test", {.spaFallback = "index.html"}));
    app.use("/strictpub", express::serveStatic("public_test", {.dotfiles = "deny", .fallthrough = false}));

    // json() + bodyLimit
    app.post("/strict", express::json({.limit = "64b"}), [](express::Req req, express::Res res) { res.json(req.json()); });
    app.post("/small", express::bodyLimit("5"), [](express::Req, express::Res res) { res.send("ok"); });

    // ======================= v0.3: security packages =======================
    static const std::string SECRET = "test-secret-123";
    app.post("/login", [](express::Req req, express::Res res) {
        auto token = express::jwt::sign(express::obj("id", 7, "role", req.json()["role"].asString()), SECRET,
                                        {.expiresIn = "1h", .issuer = "expresspp"});
        res.json(express::obj("token", token));
    });
    express::Router secure;
    secure.use(express::auth({.secret = SECRET, .cookie = "token", .issuer = "expresspp"}));
    secure.get("/me", [](express::Req req, express::Res res) { res.json(express::jwt::user(req)); });
    app.use("/sec", secure);
    app.get("/maybe", express::auth({.secret = SECRET, .credentialsRequired = false}), [](express::Req req, express::Res res) {
        res.json(express::obj("loggedIn", !express::jwt::user(req).isNull()));
    });

    app.post("/hash", [](express::Req req, express::Res res) -> express::Task<> {
        auto *loop = trantor::EventLoop::getEventLoopOfCurrentThread();
        std::string pw = req.json()["password"].asString();
        std::string h = co_await express::hash::makeAsync(pw);
        bool ok = co_await express::hash::verifyAsync(pw, h);
        bool bad = co_await express::hash::verifyAsync(pw + "x", h);
        if (trantor::EventLoop::getEventLoopOfCurrentThread() != loop) gWrongThread++;
        res.json(express::obj("alg", express::hash::algorithm(h), "ok", ok, "bad", bad));
    });

    namespace v = express::v;
    auto signup = v::object({
        {"email", v::string().trim().lowercase().email().required()},
        {"password", v::string().min(8).required()},
        {"age", v::integer().min(13)},
        {"role", v::string().oneOf({"user", "admin"}).defaultTo("user")},
        {"tags", v::array(v::string().max(5)).max(3)},
    });
    app.post("/signup", express::validate({.body = signup}),
             [](express::Req req, express::Res res) { res.json(express::validated(req, "body")); });
    app.get("/items", express::validate({.query = v::object({{"page", v::integer().min(1).defaultTo(1)},
                                                             {"active", v::boolean()}})}),
            [](express::Req req, express::Res res) { res.json(express::validated(req, "query")); });

    express::Router helmeted;
    helmeted.use(express::helmet());
    helmeted.get("/", [](express::Req, express::Res res) { res.send("safe"); });
    app.use("/helmet", helmeted);
    app.get("/helmet-nocsp", express::helmet({.contentSecurityPolicy = "", .hsts = false, .frameguard = "DENY"}),
            [](express::Req, express::Res res) { res.send("ok"); });

    static auto limiter = express::rateLimit({.windowMs = 60000, .limit = 3, .legacyHeaders = true,
                                              .keyGenerator = [](const express::Req &) { return std::string("k"); }});
    app.get("/rl", limiter, [](express::Req, express::Res res) { res.send("ok"); });
    app.get("/rl-reset", [](express::Req, express::Res res) {
        limiter.store->reset("k");
        res.send("reset");
    });
    app.get("/rl-login",
            express::rateLimit({.windowMs = 60000, .limit = 2, .skipSuccessfulRequests = true,
                                .keyGenerator = [](const express::Req &) { return std::string("login"); }}),
            [](express::Req req, express::Res res) { res.sendStatus(req.query("ok") == "1" ? 200 : 401); });

    auto disk = express::upload({.dest = "uploads_test", .limits = {.fileSize = "1kb", .files = 3}});
    auto mem = express::upload({.memory = true,
                                .fileFilter = [](const express::UploadedFile &f) { return f.mimetype.rfind("image/", 0) == 0; }});
    app.post("/up/single", disk.single("avatar"), [](express::Req req, express::Res res) {
        auto *f = express::file(req);
        std::ifstream in(f ? f->path : "", std::ios::binary);
        std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        res.json(express::obj("field", f ? f->fieldname : "", "name", f ? f->originalname : "", "size", f ? f->size : 0,
                              "mime", f ? f->mimetype : "", "content", content, "path", f ? f->path : "",
                              "title", express::fields(req)["title"]));
    });
    app.post("/up/array", disk.array("photos", 2), [](express::Req req, express::Res res) {
        res.json(express::obj("count", express::files(req).size()));
    });
    app.post("/up/mem", mem.any(), [](express::Req req, express::Res res) {
        express::Json names(::Json::arrayValue);
        for (auto &f : express::files(req)) names.append(f.originalname + ":" + std::to_string(f.buffer.size()));
        res.json(express::obj("kept", names));
    });
    app.post("/up/none", disk.none(), [](express::Req req, express::Res res) { res.json(express::fields(req)); });

#ifdef EXPRESSPP_TEST_NEVLA
    express::nv::use(app, "memory://dex_test", "dex_test");
    nevla::model("User", nevla::Schema({{"name", nevla::String().required()}, {"age", nevla::Int().min(0)}}));
    app.post("/db/users", [](express::Req req, express::Res res) -> express::Task<> {
        auto *loop = trantor::EventLoop::getEventLoopOfCurrentThread();
        auto doc = co_await Users().createAsync(express::nv::body(req));
        if (trantor::EventLoop::getEventLoopOfCurrentThread() != loop) gWrongThread++;
        res.status(201).json(doc.toJSON());
    });
    app.get("/db/users/count", [](express::Req req, express::Res res) -> express::Task<> {
        auto *loop = trantor::EventLoop::getEventLoopOfCurrentThread();
        int minAge = std::atoi(req.query("minAge", "0").c_str());
        int64_t n = co_await Users().find(nevla::obj("age", nevla::obj("$gte", minAge))).countAsync();
        if (trantor::EventLoop::getEventLoopOfCurrentThread() != loop) gWrongThread++;
        res.json(nevla::obj("count", n));
    });
    app.get("/db/users/:id", [](express::Req req, express::Res res) -> express::Task<> {
        auto doc = co_await Users().findById(req.param("id")).leanAsync();
        if (!doc) throw express::HttpError(404, "User not found");
        res.json(*doc);
    });
#endif
}

static void testEnv() {
    {
        std::ofstream f("test.env");
        f << "# comment\n"
             "export EXPRESSPP_T_PORT=8080\n"
             "EXPRESSPP_T_HOST=example.com\n"
             "EXPRESSPP_T_URL=${EXPRESSPP_T_HOST}:${EXPRESSPP_T_PORT}/api\n"
             "EXPRESSPP_T_Q=\"line1\\nline2 # not a comment\"\n"
             "EXPRESSPP_T_RAW='no ${EXPRESSPP_T_HOST}'\n"
             "EXPRESSPP_T_INLINE=value # comment\n"
             "EXPRESSPP_T_BOOL=yes\n"
             "EXPRESSPP_T_KEEP=from_file\n"
             "EXPRESSPP_T_EMPTY=\n";
    }
    setenv("EXPRESSPP_T_KEEP", "from_env", 1);
    int n = express::env::load("test.env");
    CHECK(n == 8, "env: 8 vars set (existing one kept)");
    CHECK(express::env::getInt("EXPRESSPP_T_PORT") == 8080, "env: export + int");
    CHECK(express::env::get("EXPRESSPP_T_URL") == "example.com:8080/api", "env: ${} expansion");
    CHECK(express::env::get("EXPRESSPP_T_Q") == "line1\nline2 # not a comment", "env: double quotes + escapes");
    CHECK(express::env::get("EXPRESSPP_T_RAW") == "no ${EXPRESSPP_T_HOST}", "env: single quotes are literal");
    CHECK(express::env::get("EXPRESSPP_T_INLINE") == "value", "env: inline comment");
    CHECK(express::env::getBool("EXPRESSPP_T_BOOL") == true, "env: bool");
    CHECK(express::env::get("EXPRESSPP_T_KEEP") == "from_env", "env: existing env wins");
    CHECK(express::env::load("test.env", true) == 9 && express::env::get("EXPRESSPP_T_KEEP") == "from_file", "env: override");
    CHECK(express::env::load("nope.env") == -1, "env: missing file");
    bool threw = false;
    try {
        express::env::require("EXPRESSPP_T_EMPTY");
    } catch (const std::runtime_error &) {
        threw = true;
    }
    CHECK(threw, "env: require() throws on empty");
    std::remove("test.env");

    // process.env, the Node way
    using express::process;
    unsetenv("EXPRESSPP_T_MISSING");
    setenv("EXPRESSPP_T_BLANK", "", 1);
    std::string url = process.env["EXPRESSPP_T_HOST"] || "*";
    CHECK(url == "example.com", "process.env: value || default -> value");
    CHECK((process.env["EXPRESSPP_T_MISSING"] || "*") == "*", "process.env: missing -> default");
    CHECK((process.env["EXPRESSPP_T_BLANK"] || "*") == "*", "process.env: empty -> default (like Node)");
    auto port = process.env["EXPRESSPP_T_PORT"] || 3000;
    CHECK(port == 8080 && (process.env["EXPRESSPP_T_MISSING"] || 3000) == 3000, "process.env: number default");
    CHECK((process.env["EXPRESSPP_T_BOOL"] || false) == true && (process.env["EXPRESSPP_T_MISSING"] || true) == true,
          "process.env: bool default");
    CHECK((process.env["EXPRESSPP_T_MISSING"] || 2.5) == 2.5, "process.env: double default");
    std::string plain = process.env["EXPRESSPP_T_MISSING"];
    CHECK(plain.empty() && process.env["EXPRESSPP_T_HOST"] == "example.com", "process.env: plain read + ==");
    process.env["EXPRESSPP_T_SET"] = "yes";
    CHECK(express::env::get("EXPRESSPP_T_SET") == "yes" && process.env["EXPRESSPP_T_SET"].exists(), "process.env: assignment");
    bool threwNum = false;
    try {
        (void)(process.env["EXPRESSPP_T_HOST"] || 3000);
    } catch (const std::runtime_error &) {
        threwNum = true;
    }
    CHECK(threwNum, "process.env: not-a-number with number default throws");

    // the README promises both ways give the same answer
    setenv("EXPRESSPP_T_NUM", "42", 1);
    setenv("EXPRESSPP_T_D", "2.5", 1);
    bool same = (process.env["EXPRESSPP_T_HOST"] || "*") == express::env::get("EXPRESSPP_T_HOST", "*") &&
                (process.env["EXPRESSPP_T_MISSING"] || "*") == express::env::get("EXPRESSPP_T_MISSING", "*") &&
                (process.env["EXPRESSPP_T_BLANK"] || "*") == express::env::get("EXPRESSPP_T_BLANK", "*") &&
                (process.env["EXPRESSPP_T_NUM"] || 1) == express::env::getInt("EXPRESSPP_T_NUM", 1) &&
                (process.env["EXPRESSPP_T_BLANK"] || 7) == express::env::getInt("EXPRESSPP_T_BLANK", 7) &&
                (process.env["EXPRESSPP_T_D"] || 1.0) == express::env::getDouble("EXPRESSPP_T_D", 1.0) &&
                (process.env["EXPRESSPP_T_BOOL"] || false) == express::env::getBool("EXPRESSPP_T_BOOL", false) &&
                (process.env["EXPRESSPP_T_MISSING"] || true) == express::env::getBool("EXPRESSPP_T_MISSING", true) &&
                std::string(process.env["EXPRESSPP_T_MISSING"]) == express::env::get("EXPRESSPP_T_MISSING") &&
                process.env["EXPRESSPP_T_HOST"].exists() == express::env::has("EXPRESSPP_T_HOST") &&
                process.env["EXPRESSPP_T_MISSING"].exists() == express::env::has("EXPRESSPP_T_MISSING");
    CHECK(same, "env: process.env and env:: give the same results");
    int portA = process.env["EXPRESSPP_T_NUM"] || 3000;
    int portB = express::env::getInt("EXPRESSPP_T_NUM", 3000);
    CHECK(portA == 42 && portB == 42, "env: README int examples compile and match");
}

// a plain Drogon WebSocket controller living next to the Express++ app
class EchoWs : public drogon::WebSocketController<EchoWs> {
  public:
    void handleNewMessage(const drogon::WebSocketConnectionPtr &c, std::string &&m, const drogon::WebSocketMessageType &t) override {
        if (t == drogon::WebSocketMessageType::Text) c->send("echo:" + m);
    }
    void handleNewConnection(const drogon::HttpRequestPtr &, const drogon::WebSocketConnectionPtr &) override {}
    void handleConnectionClosed(const drogon::WebSocketConnectionPtr &) override {}
    WS_PATH_LIST_BEGIN
    WS_PATH_ADD("/ws-echo");
    WS_PATH_LIST_END
};

static void testSecurityUnits() {
    // jwt.io's example token, signed by other libraries: proves we are compatible
    const std::string jwtio =
        "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJzdWIiOiIxMjM0NTY3ODkwIiwibmFtZSI6IkpvaG4gRG9lIiwiaWF0IjoxNTE2MjM5MDIyfQ."
        "SflKxwRJSMeKKF2QT4fwpMeJf36POk6yJV_adQssw5c";
    auto p = express::jwt::verify(jwtio, "your-256-bit-secret");
    CHECK(p["name"].asString() == "John Doe" && p["iat"].asInt64() == 1516239022, "jwt: verifies jwt.io token");
    auto expectThrow = [](auto fn, const std::string &msg) {
        try {
            fn();
        } catch (const express::jwt::Error &e) {
            return std::string(e.what()) == msg && e.status() == 401;
        } catch (...) {
            return false;
        }
        return false;
    };
    CHECK(expectThrow([&] { express::jwt::verify(jwtio, "wrong"); }, "invalid signature"), "jwt: wrong secret");
    CHECK(expectThrow([&] { express::jwt::verify("abc.def", "s"); }, "jwt malformed"), "jwt: malformed");
    CHECK(expectThrow([&] { express::jwt::verify("", "s"); }, "jwt must be provided"), "jwt: empty");
    CHECK(expectThrow([&] { express::jwt::verify(jwtio, "your-256-bit-secret", {.algorithms = {"HS512"}}); },
                      "invalid algorithm"),
          "jwt: algorithm allow-list");
    // alg: none token
    std::string none = "eyJhbGciOiJub25lIiwidHlwIjoiSldUIn0.eyJzdWIiOiJ4In0.";
    CHECK(expectThrow([&] { express::jwt::verify(none, "s"); }, "jwt signature is required"), "jwt: alg none rejected");
    auto expired = express::jwt::sign(express::obj("a", 1, "exp", 1000), "s");
    bool expiredThrown = false;
    try {
        express::jwt::verify(expired, "s");
    } catch (const express::jwt::TokenExpiredError &e) {
        expiredThrown = e.expiredAt() == 1000;
    }
    CHECK(expiredThrown, "jwt: TokenExpiredError");
    CHECK(express::jwt::verify(expired, "s", {.ignoreExpiration = true})["a"].asInt() == 1, "jwt: ignoreExpiration");
    auto future = express::jwt::sign(express::obj("a", 1), "s", {.notBefore = "1h"});
    bool nbf = false;
    try {
        express::jwt::verify(future, "s");
    } catch (const express::jwt::NotBeforeError &) {
        nbf = true;
    }
    CHECK(nbf, "jwt: NotBeforeError");
    auto aud = express::jwt::sign(express::obj("a", 1), "s", {.audience = "app1"});
    CHECK(expectThrow([&] { express::jwt::verify(aud, "s", {.audience = "app2"}); }, "jwt audience invalid. expected: app2") &&
              express::jwt::verify(aud, "s", {.audience = "app1"})["aud"].asString() == "app1",
          "jwt: audience");
    for (auto alg : {"HS384", "HS512"}) {
        auto t = express::jwt::sign(express::obj("x", alg), "k", {.algorithm = alg});
        CHECK(express::jwt::verify(t, "k", {.algorithms = {alg}})["x"].asString() == alg, std::string("jwt: ") + alg);
    }
    CHECK(express::jwt::decode(jwtio)["sub"].asString() == "1234567890" && express::jwt::decodeHeader(jwtio)["alg"] == "HS256",
          "jwt: decode");
    CHECK(express::jwt::parseDuration("7d") == 604800 && express::jwt::parseDuration("15m") == 900 &&
              express::jwt::parseDuration("90") == 90 && express::jwt::parseDuration("2 hours") == 7200,
          "jwt: parseDuration");
    // RS256 with a fresh key pair
    if (std::system("openssl genpkey -algorithm RSA -pkeyopt rsa_keygen_bits:2048 -out rs_priv.pem 2>/dev/null && "
                    "openssl pkey -in rs_priv.pem -pubout -out rs_pub.pem 2>/dev/null") == 0) {
        auto read = [](const char *f) {
            std::ifstream in(f);
            return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        };
        std::string priv = read("rs_priv.pem"), pub = read("rs_pub.pem");
        auto t = express::jwt::sign(express::obj("u", "rsa"), priv, {.algorithm = "RS256"});
        CHECK(express::jwt::verify(t, pub, {.algorithms = {"RS256"}})["u"].asString() == "rsa", "jwt: RS256 sign/verify");
        CHECK(expectThrow([&] { express::jwt::verify(t + "A", pub, {.algorithms = {"RS256"}}); }, "invalid signature"),
              "jwt: RS256 tampered");
        CHECK(expectThrow([&] { express::jwt::verify(t, pub); }, "invalid algorithm"), "jwt: RS256 needs allow-list");
        std::remove("rs_priv.pem");
        std::remove("rs_pub.pem");
    }

    // hash
    std::string h = express::hash::make("pässwörd");
    CHECK(h.rfind("$argon2id$v=19$m=65536,t=3,p=1$", 0) == 0, "hash: standard argon2id string");
    CHECK(express::hash::verify("pässwörd", h) && !express::hash::verify("password", h), "hash: argon2id verify");
    CHECK(!express::hash::needsRehash(h) && express::hash::needsRehash(express::hash::make("x", {.opsLimit = 2})),
          "hash: needsRehash");
    std::string b = express::hash::bcrypt("hunter2", 4);
    CHECK(b.rfind("$2b$04$", 0) == 0 && express::hash::verify("hunter2", b) && !express::hash::verify("hunter3", b),
          "hash: bcrypt");
    std::string b2a = "$2a$" + b.substr(4);  // Node bcrypt / bcryptjs write $2a$
    CHECK(express::hash::verify("hunter2", b2a) && express::hash::algorithm(b2a) == "bcrypt" &&
              express::hash::needsRehash(b2a),
          "hash: $2a$ hashes from Node verify");
    CHECK(!express::hash::verify("x", "garbage") && express::hash::algorithm("garbage").empty(), "hash: junk -> false");
    CHECK(express::offload([] { return 41 + 1; }).get() == 42, "offload: get()");

    // validate (standalone)
    namespace v = express::v;
    auto s = v::object({{"name", v::string().required()},
                        {"nick", v::string().nullable()},
                        {"score", v::number().max(10)},
                        {"even", v::integer().custom([](express::Json &x) { return x.asInt() % 2 ? "must be even" : ""; })},
                        {"meta", v::object({{"k", v::string().uuid()}}).strict()}});
    auto r = s.validate(express::obj("name", "a", "nick", nullptr, "score", 10.5, "even", 3,
                                     "meta", express::obj("k", "not-uuid", "z", 1)));
    CHECK(!r.ok && r.errors["score"] == "must be less than or equal to 10" && r.errors["even"] == "must be even" &&
              r.errors["meta.k"] == "must be a valid UUID" && r.errors["meta.z"] == "is not allowed" && !r.errors.count("nick"),
          "validate: number/custom/nested strict/nullable");
    auto ok = s.validate(express::obj("name", "a", "even", 4));
    CHECK(ok.ok && ok.value["even"].asInt() == 4 && !ok.value.isMember("score"), "validate: ok + optional fields left out");
    CHECK(!v::integer().validate(express::Json(2.5)).ok && v::number().validate(express::Json(2.5)).ok, "validate: integer vs number");
    CHECK(v::array(v::integer()).validate(express::Json("1,2,3"), {.coerce = true}).value.size() == 3,
          "validate: coerce comma list");
    CHECK(!v::array().unique().validate(express::arr(1, 1)).ok, "validate: unique");
    CHECK(v::string().message("bad!").min(3).validate(express::Json("ab")).errors[""] == "bad!", "validate: message()");
}


static void testCrypto() {
    namespace crypto = express::crypto;
    CHECK(crypto::createHash("sha256").update("abc").digest("hex") ==
              "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
          "crypto: sha256(abc)");
    CHECK(crypto::createHash("md5").digest("hex") == "d41d8cd98f00b204e9800998ecf8427e", "crypto: md5('')");
    CHECK(crypto::createHash("sha512").update("abc").digest("base64url") ==
              "3a81oZNherrMQXNJriBBMRLm-k6JqX6iCp7u5ktV05ohkpkqJ0_BqDa6PCOj_uu9RU1EI2Q86A4qmslPpUyknw",
          "crypto: sha512 base64url (Node vector)");
    CHECK(crypto::createHash("sha256").update("a").update("bc").digest("hex") ==
              crypto::createHash("sha256").update("abc").digest("hex"),
          "crypto: update() chaining");
    CHECK(crypto::createHmac("sha256", "key").update("The quick brown fox jumps over the lazy dog").digest("hex") ==
              "f7bc83f430538424b13298e6aa6fb143ef4d59a14946175997479dbc2d1a3cd8",
          "crypto: HMAC-SHA256");
    CHECK(crypto::pbkdf2Sync("password", "salt", 1, 20, "sha1").toString("hex") ==
              "0c60c80f961f0e71f3a9b524af6012062fe037a6",
          "crypto: PBKDF2-SHA1 RFC 6070");
    CHECK(crypto::pbkdf2Sync("password", "salt", 1000, 32).toString("hex") ==
              "632c2812e46d4604102ba7618e9d6d7d2f8128f6266b4a03264d2a0460b7dcb3",
          "crypto: pbkdf2 sha256 (Node vector)");
    CHECK(crypto::scryptSync("password", "salt", 16).toString("hex") == "745731af4484f323968969eda289aeee",
          "crypto: scryptSync (Node vector)");
    CHECK(crypto::Buffer::from("hello?>").toString("base64") == "aGVsbG8/Pg==" &&
              crypto::Buffer::from("hello?>").toString("base64url") == "aGVsbG8_Pg" &&
              crypto::Buffer::from("aGVsbG8_Pg", "base64url").toString() == "hello?>" &&
              crypto::Buffer::from("aGVsbG8/Pg==", "base64").toString() == "hello?>" &&
              crypto::Buffer::from("68690a", "hex").toString() == "hi\n",
          "crypto: Buffer encodings");
    auto rb = crypto::randomBytes(32);
    CHECK(rb.size() == 32 && rb.toString("hex").size() == 64 && !(rb == crypto::randomBytes(32)), "crypto: randomBytes");
    std::string uuid = crypto::randomUUID();
    CHECK(uuid.size() == 36 && uuid[14] == '4' && std::string("89ab").find(uuid[19]) != std::string::npos &&
              uuid[8] == '-' && uuid != crypto::randomUUID(),
          "crypto: randomUUID v4");
    bool inRange = true;
    std::set<long long> seen;
    for (int i = 0; i < 2000; i++) {
        long long v = crypto::randomInt(1, 7);
        inRange = inRange && v >= 1 && v < 7;
        seen.insert(v);
    }
    CHECK(inRange && seen.size() == 6, "crypto: randomInt range");
    CHECK(crypto::timingSafeEqual(std::string("abc"), std::string("abc")) &&
              !crypto::timingSafeEqual(std::string("abc"), std::string("abd")) &&
              !crypto::timingSafeEqual(std::string("abc"), std::string("ab")),
          "crypto: timingSafeEqual");

    // AES-256-GCM, checked against Node: same key/iv/aad -> same ciphertext and tag
    auto key = crypto::Buffer(std::string(32, '\x07'));
    auto iv = crypto::Buffer(std::string(12, '\x01'));
    auto c = crypto::createCipheriv("aes-256-gcm", key, iv);
    c.setAAD("hdr");
    std::string enc = c.update("secret data", "utf8", "hex") + c.final("hex");
    auto tag = c.getAuthTag();
    CHECK(enc == "0584eac5f5cbcc6ab0a0bd" && tag.toString("hex") == "2512d6d0ecc3c81391d6273fbb68cdec",
          "crypto: aes-256-gcm matches Node");
    auto d = crypto::createDecipheriv("aes-256-gcm", key, iv);
    d.setAAD("hdr").setAuthTag(tag);
    std::string plain = d.update(enc, "hex", "utf8") + d.final("utf8");
    CHECK(plain == "secret data", "crypto: aes-256-gcm decrypt");
    bool tamperCaught = false;
    try {
        auto d2 = crypto::createDecipheriv("aes-256-gcm", key, iv);
        d2.setAAD("hdr").setAuthTag(tag);
        std::string bad = enc;
        bad[0] = bad[0] == '0' ? '1' : '0';
        std::string out = d2.update(bad, "hex", "utf8") + d2.final("utf8");
    } catch (const crypto::Error &) {
        tamperCaught = true;
    }
    CHECK(tamperCaught, "crypto: gcm tamper detected");

    auto k2 = crypto::Buffer(std::string(32, '\x03'));
    auto iv2 = crypto::Buffer(std::string(16, '\x05'));
    auto cb = crypto::createCipheriv("aes-256-cbc", k2, iv2);
    std::string cbc = cb.update("hello cbc mode!!x", "utf8", "base64") + cb.final("base64");
    CHECK(cbc == "EXjczbgnBkP5bcXXgDg3dJoy2d6XO6o9J0aMIO/k7ck=", "crypto: aes-256-cbc matches Node");
    auto dcb = crypto::createDecipheriv("aes-256-cbc", k2, iv2);
    CHECK(dcb.update(cbc, "base64", "utf8") + dcb.final("utf8") == "hello cbc mode!!x", "crypto: aes-256-cbc decrypt");

    auto ck = crypto::randomBytes(32), civ = crypto::randomBytes(12);
    auto cc = crypto::createCipheriv("chacha20-poly1305", ck, civ);
    auto ct = cc.update(crypto::Buffer::from("chacha!"));
    auto ctEnd = cc.final();
    auto ctag = cc.getAuthTag();
    auto dc = crypto::createDecipheriv("chacha20-poly1305", ck, civ);
    dc.setAuthTag(ctag);
    CHECK(dc.update(ct.bytes() + ctEnd.bytes(), "binary", "utf8") + dc.final("utf8") == "chacha!",
          "crypto: chacha20-poly1305 roundtrip");
    bool badKey = false;
    try {
        crypto::createCipheriv("aes-256-gcm", crypto::randomBytes(16), iv);
    } catch (const crypto::Error &) {
        badKey = true;
    }
    CHECK(badKey, "crypto: wrong key length throws");
    {  // final() written first must still work (operand order of + is unspecified in C++)
        auto e1 = crypto::createCipheriv("aes-256-cbc", k2, iv2);
        auto fin = e1.final("base64");
        std::string head = e1.update("hello cbc mode!!x", "utf8", "base64");
        CHECK(head + fin == cbc, "crypto: final() is lazy, order-safe");
    }

    // cookie-signature, same output as Node's cookie-signature
    CHECK(express::cookieSign("42", "new-secret") == "42.GjccyhUPYmHiB6XVVjur8mjQWHrNIIDlyhErlLud1wo",
          "cookieSign matches Node");
    CHECK(express::cookieUnsign("42.GjccyhUPYmHiB6XVVjur8mjQWHrNIIDlyhErlLud1wo", "new-secret").value_or("") == "42" &&
              !express::cookieUnsign("43.GjccyhUPYmHiB6XVVjur8mjQWHrNIIDlyhErlLud1wo", "new-secret") &&
              !express::cookieUnsign("42.GjccyhUPYmHiB6XVVjur8mjQWHrNIIDlyhErlLud1wo", "wrong"),
          "cookieUnsign");
}

static void testJson() {
    auto j = express::obj("a", 1, "b", "x", "c", true, "d", 2.5, "e", express::arr(1, "two"), "f", nullptr, "g", int64_t(1) << 40);
    CHECK(express::stringify(j) == R"({"a":1,"b":"x","c":true,"d":2.5,"e":[1,"two"],"f":null,"g":1099511627776})",
          "json: obj/arr");
#ifdef EXPRESSPP_TEST_NEVLA
    auto fromNevla = express::toJsonValue(nevla::obj("n", 5, "s", "y"));
    CHECK(fromNevla["n"].asInt() == 5 && fromNevla["s"].asString() == "y", "json: nevla::Value converts");
#endif
}

static void runChecks() {
    // basics
    auto r = call(Get, "/");
    CHECK(r.status == 200 && r.body == "hello", "GET / -> hello");
    CHECK(r.header("content-type").find("text/html") != std::string::npos, "send() is text/html");
    r = call(Head, "/");
    CHECK(r.status == 200, "HEAD uses the GET route");
    r = call(Get, "/users/42?page=3");
    CHECK(r.status == 200 && r.json()["id"].asString() == "42" && r.json()["page"].asString() == "3", "params + query");
    r = call(Post, "/echo", R"({"x":[1,2],"y":"z"})");
    CHECK(r.status == 200 && r.json()["x"][1].asInt() == 2 && r.json()["y"].asString() == "z", "json body echo");
    r = call(Get, "/trace");
    CHECK(r.json()["trace"].asString() == "a,b,co", "middleware order (incl. coroutine middleware)");
    r = call(Get, "/files/a/b/c.txt");
    CHECK(r.json()["rest"].asString() == "a/b/c.txt", "wildcard");
    r = call(Get, "/nope");
    CHECK(r.status == 404 && r.body == "Cannot GET /nope", "default 404");
    r = call(Post, "/");
    CHECK(r.status == 404 && r.body == "Cannot POST /", "method mismatch -> 404");

    // errors
    r = call(Get, "/teapot");
    CHECK(r.status == 418 && r.json()["error"].asString() == "I'm a teapot", "HttpError");
    r = call(Get, "/boom");
    CHECK(r.status == 500 && r.json()["error"].asString() == "kaboom", "exception -> 500");
    r = call(Get, "/denied");
    CHECK(r.status == 401 && r.json()["error"].asString() == "nope", "next.error()");
    r = call(Get, "/coboom");
    CHECK(r.status == 422 && r.json()["error"].asString() == "late failure", "throw after co_await");

    // route middleware
    r = call(Get, "/private");
    CHECK(r.status == 401, "auth middleware blocks");
    r = call(Get, "/private", "", {{"Authorization", "Bearer secret"}});
    CHECK(r.status == 200 && r.json()["user"].asString() == "asha", "auth middleware passes data on");

    // response helpers
    r = call(Get, "/nothing");
    CHECK(r.status == 204 && r.body.empty(), "sendStatus(204)");
    r = call(Get, "/go");
    CHECK(r.status == 302 && r.header("location") == "/elsewhere", "redirect");
    r = call(Get, "/cookie");
    CHECK(r.status == 200 && r.header("x-express") == "1" && r.header("content-type").find("text/plain") == 0 &&
              r.raw && r.raw->getCookie("sid").value() == "abc",
          "cookie + header + type");

    // cookie-parser
    r = call(Get, "/cp/set");
    CHECK(r.status == 200 && r.raw && r.raw->getCookie("uid").value() == "s%3A42.GjccyhUPYmHiB6XVVjur8mjQWHrNIIDlyhErlLud1wo" &&
              r.raw->getCookie("theme").value() == "dark%20mode",
          "cookieParser: signed Set-Cookie matches Node format");
    r = call(Get, "/cp/read", "",
             {{"Cookie", "uid=s%3A42.GjccyhUPYmHiB6XVVjur8mjQWHrNIIDlyhErlLud1wo; theme=dark%20mode; "
                         "note=s%3Ahello%20world.CDu9j9XuYc1tdtqhBB%2BmCI%2F71Lzp0jgeetd50%2BE%2BZ%2Fk"}});
    CHECK(r.json()["uid"].asString() == "42" && r.json()["note"].asString() == "hello world" &&
              r.json()["theme"].asString() == "dark mode" && !r.json()["uidInPlain"].asBool() &&
              r.json()["signedCount"].asInt() == 2 && r.json()["secret"].asString() == "new-secret",
          "cookieParser: reads Node-signed cookies");
    r = call(Get, "/cp/read", "", {{"Cookie", "uid=s%3A42.G7lGWMvDsvQyB%2BbeoK2ETuEHC9CzP7AvAJsrJWcN0sA"}});
    CHECK(r.json()["uid"].asString() == "42", "cookieParser: old secret still accepted (rotation)");
    r = call(Get, "/cp/read", "", {{"Cookie", "uid=s%3A1.GjccyhUPYmHiB6XVVjur8mjQWHrNIIDlyhErlLud1wo"}});
    CHECK(r.json()["uid"].asString() == "<none>" && !r.json()["uidInPlain"].asBool(),
          "cookieParser: tampered cookie rejected");
    r = call(Get, "/unsigned-sign");
    CHECK(r.status == 500, "signed cookie without cookieParser -> 500");
    r = call(Get, "/plain-cookies", "", {{"Cookie", "a=x%2Fy; b=2"}});
    CHECK(r.json()["a"].asString() == "x/y" && r.json()["n"].asInt() == 2 && r.json()["one"].asString() == "x/y",
          "req.cookies() without cookieParser");
    r = call(Get, "/twice");
    CHECK(r.status == 200 && r.body == "first", "second send ignored");
    r = call(Get, "/thread");
    CHECK(r.status == 200 && r.json()["from"].asString() == "thread", "send from another thread");

    // routers
    r = call(Get, "/api/v2/items/9");
    auto j = r.json();
    CHECK(r.status == 200 && j["ver"].asString() == "v2" && j["item"].asString() == "9" &&
              j["baseUrl"].asString() == "/api/v2" && j["path"].asString() == "/items/9" &&
              j["router"].asString() == "api",
          "mounted router: params from both levels, baseUrl, path");
    r = call(Get, "/api/v2/deep");
    CHECK(r.status == 200 && r.json()["baseUrl"].asString() == "/api/v2/deep", "router inside router");
    r = call(Get, "/after-api");
    CHECK(r.status == 200 && r.json()["path"].asString() == "/after-api" && r.json()["baseUrl"].asString() == "",
          "path/baseUrl restored after a router");

    // cors
    r = call(Options, "/users/1", "", {{"Origin", "http://good.example"}, {"Access-Control-Request-Method", "PUT"},
                                       {"Access-Control-Request-Headers", "content-type,authorization"}});
    CHECK(r.status == 204 && r.header("access-control-allow-origin") == "http://good.example" &&
              r.header("access-control-allow-headers") == "content-type,authorization" &&
              r.header("access-control-allow-credentials") == "true" && r.header("access-control-max-age") == "600",
          "cors preflight");
    r = call(Get, "/", "", {{"Origin", "http://evil.example"}});
    CHECK(r.status == 200 && r.header("access-control-allow-origin").empty(), "cors: unknown origin gets no header");
    r = call(Get, "/", "", {{"Origin", "http://good.example"}});
    CHECK(r.header("access-control-allow-origin") == "http://good.example", "cors: allowed origin");


    // ======================= v0.2 checks =======================
    r = call(Get, "/books");
    CHECK(r.body == "list", "route(): get");
    r = call(Post, "/books");
    CHECK(r.status == 201 && r.body == "create", "route(): post");
    r = call(Delete, "/books");
    CHECK(r.body == "remove", "route(): del");
    r = call(Put, "/books");
    CHECK(r.status == 404, "route(): other methods 404");

    r = call(Get, "/opt/1/2");
    CHECK(r.json()["a"].asString() == "1" && r.json()["b"].asString() == "2", "optional param present");
    r = call(Get, "/opt/1");
    CHECK(r.status == 200 && r.json()["b"].asString() == "none", "optional param missing");
    r = call(Get, "/opt/1/2/3");
    CHECK(r.status == 404, "optional param: too many segments");
    r = call(Get, "/num/42");
    CHECK(r.json()["num"].asString() == "42", ":id(\\d+) matches digits");
    r = call(Get, "/num/abc");
    CHECK(r.json()["slug"].asString() == "abc", ":id(\\d+) skips non-digits");
    r = call(Get, "/v3/status");
    CHECK(r.json()["version"].asString() == "3", "regex path + group param");
    r = call(Get, "/vx/status");
    CHECK(r.status == 404, "regex path no match");

    gParamCalls = 0;
    r = call(Get, "/u/7");
    CHECK(r.json()["user"].asString() == "user-7" && gParamCalls == 1, "app.param runs once per request");
    r = call(Get, "/u/bad");
    CHECK(r.status == 404 && r.json()["error"].asString() == "no such user", "app.param error");

    r = call(Get, "/errapi/fail");
    CHECK(r.status == 503 && r.json()["apiError"].asString() == "api broke" &&
              r.json()["baseUrl"].asString() == "/errapi",
          "router error middleware (chain of two, async one handles)");
    r = call(Get, "/errapi/late");
    CHECK(r.status == 503 && r.json()["apiError"].asString() == "late api error", "error after co_await -> error middleware");
    r = call(Get, "/errapi/conflict");
    CHECK(r.status == 409 && r.json()["error"].asString() == "rethrown: conflict", "error middleware rethrows -> app");
    r = call(Get, "/rec/x");
    CHECK(r.status == 200 && r.json()["recovered"].asBool(), "error middleware next() recovers");
    r = call(Get, "/boom");
    CHECK(r.status == 500, "errors outside routers still reach the app");

    r = call(Get, "/dl");
    CHECK(r.status == 200 && r.body == "hello file\n" &&
              r.header("content-disposition") == "attachment; filename=\"Report.txt\"",
          "download()");
    r = call(Get, "/attach");
    CHECK(r.header("content-disposition") == "attachment; filename=\"data.csv\"" &&
              r.header("content-type").find("text/csv") == 0 && r.body == "a,b",
          "attachment() + type(csv)");
    r = call(Get, "/render");
    CHECK(r.status == 200 && r.header("content-type").find("text/html") == 0 &&
              r.body == "<h1>MySite</h1>\nHi &lt;Asha&gt; <b>x</b> [x][y] empty Durgapur locals\n",
          "render(): partial, escape, raw, loop, inverted, dotted, app.locals, res.locals");
    r = call(Get, "/render-engine");
    CHECK(r.body == "ENGINE:eng:1", "app.engine() custom engine");
    r = call(Get, "/render-missing");
    CHECK(r.status == 500 && r.json()["error"].asString().find("failed to lookup view") != std::string::npos,
          "render(): missing view -> error");
    r = call(Get, "/fmt", "", {{"Accept", "application/json"}});
    CHECK(r.json()["hi"].asBool() && r.header("vary") == "Origin, Accept", "format(): json (+ Vary: Accept)");
    r = call(Get, "/fmt", "", {{"Accept", "text/html,application/xhtml+xml;q=0.9,*/*;q=0.8"}});
    CHECK(r.body == "<p>hi</p>" && r.header("content-type").find("text/html") == 0, "format(): browser -> html");
    r = call(Get, "/fmt", "", {{"Accept", "image/png"}});
    CHECK(r.status == 406, "format(): nothing acceptable -> 406");
    r = call(Get, "/fmt-default", "", {{"Accept", "image/png"}});
    CHECK(r.body == "fallback", "format(): default");
    r = call(Get, "/headers");
    CHECK(r.status == 201 && r.header("x-list") == "a, b" && r.header("vary") == "Origin, Accept" &&
              r.header("location") == "/there",
          "append(), vary(), location()");

    r = call(Post, "/reqinfo", R"({"a":1})",
             {{"Host", "api.example.com:8080"}, {"X-Requested-With", "XMLHttpRequest"},
              {"Accept", "text/html;q=0.5, application/json"}});
    j = r.json();
    CHECK(j["hostname"].asString() == "api.example.com" && j["protocol"].asString() == "http" && !j["secure"].asBool() &&
              j["xhr"].asBool() && j["accepts"].asString() == "json" && j["isJson"].asBool() && !j["isHtml"].asBool(),
          "req.hostname/protocol/xhr/accepts/is");
    r = call(Post, "/reqinfo", R"({"a":1})",
             {{"X-Forwarded-For", "203.0.113.9, 10.0.0.1"}, {"X-Forwarded-Proto", "https"},
              {"X-Forwarded-Host", "shop.example"}});
    j = r.json();
    CHECK(j["ip"].asString() == "203.0.113.9" && j["protocol"].asString() == "https" && j["secure"].asBool() &&
              j["hostname"].asString() == "shop.example",
          "trust proxy: X-Forwarded-*");

    r = call(Get, "/pub/a.txt");
    CHECK(r.status == 200 && r.body == "hello file\n" && r.header("cache-control") == "public, max-age=60" &&
              !r.header("etag").empty() && !r.header("last-modified").empty(),
          "serveStatic: file + cache headers");
    std::string etag = r.header("etag");
    r = call(Get, "/pub/a.txt", "", {{"If-None-Match", etag}});
    CHECK(r.status == 304 && r.body.empty(), "serveStatic: ETag -> 304");
    r = call(Get, "/pub/");
    CHECK(r.status == 200 && r.body == "<p>index</p>\n", "serveStatic: index.html");
    r = call(Get, "/pub/page");
    CHECK(r.status == 200 && r.body == "<p>page</p>\n", "serveStatic: extensions");
    r = call(Get, "/pub/sub/b.css");
    CHECK(r.status == 200 && r.header("content-type").find("text/css") == 0, "serveStatic: subfolder + mime");
    r = call(Get, "/pub/.secret");
    CHECK(r.status == 404 && r.body.find("Cannot GET") == 0, "serveStatic: dotfile ignored -> falls through");
    r = call(Get, "/strictpub/.secret");
    CHECK(r.status == 403, "serveStatic: dotfiles deny -> 403");
    r = call(Get, "/strictpub/missing.txt");
    CHECK(r.status == 404 && r.json()["error"].asString() == "Not Found", "serveStatic: fallthrough=false -> 404");
    r = call(Get, "/pub/..%2f..%2fCMakeCache.txt");
    CHECK(r.status == 403 || r.status == 404, "serveStatic: ../ cannot escape the root");
    r = call(Get, "/spa/dashboard/settings", "", {{"Accept", "text/html"}});
    CHECK(r.status == 200 && r.body == "<p>index</p>\n", "serveStatic: SPA fallback");
    r = call(Get, "/spa/api/data.json", "", {{"Accept", "application/json"}});
    CHECK(r.status == 404, "serveStatic: SPA fallback only for HTML requests");
    r = call(Post, "/pub/a.txt");
    CHECK(r.status == 404, "serveStatic: POST passes through");

    r = call(Post, "/strict", R"({"ok":1})");
    CHECK(r.status == 200 && r.json()["ok"].asInt() == 1, "json(): valid");
    r = call(Post, "/strict", R"({"ok": nope})");
    CHECK(r.status == 400 && r.json()["error"].asString().find("Invalid JSON") == 0, "json(): invalid -> 400");
    r = call(Post, "/strict", "123");
    CHECK(r.status == 400, "json(): strict top level");
    r = call(Post, "/strict", R"({"long":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"})");
    CHECK(r.status == 413, "json(): limit -> 413");
    r = call(Post, "/small", R"({"a":1})");
    CHECK(r.status == 413, "bodyLimit() -> 413");

    // ======================= v0.3 checks =======================
    r = call(Post, "/login", R"({"role":"admin"})");
    std::string token = r.json()["token"].asString();
    CHECK(r.status == 200 && std::count(token.begin(), token.end(), '.') == 2, "jwt: login issues a token");
    r = call(Get, "/sec/me", "", {{"Authorization", "Bearer " + token}});
    CHECK(r.status == 200 && r.json()["id"].asInt() == 7 && r.json()["role"].asString() == "admin" &&
              r.json()["iss"].asString() == "expresspp" && r.json().isMember("exp"),
          "auth(): Bearer token -> req user");
    r = call(Get, "/sec/me");
    CHECK(r.status == 401 && r.json()["error"].asString() == "No authorization token was found", "auth(): no token -> 401");
    r = call(Get, "/sec/me", "", {{"Authorization", "Bearer " + token + "x"}});
    CHECK(r.status == 401 && r.json()["error"].asString() == "invalid signature", "auth(): tampered -> 401");
    r = call(Get, "/sec/me", "", {{"Authorization", "Token abc"}});
    CHECK(r.status == 401, "auth(): wrong scheme -> 401");
    r = call(Get, "/sec/me", "", {{"Cookie", "token=" + token}});
    CHECK(r.status == 200 && r.json()["id"].asInt() == 7, "auth(): token from cookie");
    r = call(Get, "/maybe");
    CHECK(r.status == 200 && !r.json()["loggedIn"].asBool(), "auth(): credentialsRequired=false");
    r = call(Get, "/maybe", "", {{"Authorization", "Bearer " + token}});
    CHECK(r.json()["loggedIn"].asBool(), "auth(): optional with token");

    r = call(Post, "/hash", R"({"password":"s3cret!"})");
    CHECK(r.status == 200 && r.json()["alg"].asString() == "argon2id" && r.json()["ok"].asBool() && !r.json()["bad"].asBool(),
          "hash: makeAsync/verifyAsync (argon2id)");

    r = call(Post, "/signup", R"({"email":"  Asha@Example.COM ","password":"longenough","age":20,"extra":"x"})");
    j = r.json();
    CHECK(r.status == 200 && j["email"].asString() == "asha@example.com" && j["role"].asString() == "user" &&
              !j.isMember("extra") && j["age"].asInt() == 20,
          "validate: trim/lowercase, default, unknown key removed");
    r = call(Post, "/signup", R"({"email":"nope","password":"short","age":10,"role":"boss","tags":["ok","toolong"]})");
    j = r.json()["errors"];
    CHECK(r.status == 400 && r.json()["error"].asString() == "Validation failed" &&
              j["email"].asString() == "must be a valid email" &&
              j["password"].asString() == "must be at least 8 characters" &&
              j["age"].asString() == "must be greater than or equal to 13" &&
              j["role"].asString() == "must be one of [user, admin]" &&
              j["tags.1"].asString() == "must be at most 5 characters",
          "validate: field errors (400)");
    r = call(Post, "/signup", "");
    CHECK(r.status == 400 && r.json()["errors"]["email"].asString() == "is required", "validate: missing body -> required");
    r = call(Get, "/items?page=2&active=true");
    CHECK(r.status == 200 && r.json()["page"].isIntegral() && r.json()["page"].asInt() == 2 && r.json()["active"].asBool(),
          "validate: query coercion");
    r = call(Get, "/items");
    CHECK(r.status == 200 && r.json()["page"].asInt() == 1, "validate: query default");
    r = call(Get, "/items?page=abc");
    CHECK(r.status == 400 && r.json()["errors"]["page"].asString() == "must be an integer", "validate: query error");

    r = call(Get, "/helmet");
    CHECK(r.status == 200 && r.header("content-security-policy").find("default-src 'self'") == 0 &&
              r.header("x-frame-options") == "SAMEORIGIN" && r.header("x-content-type-options") == "nosniff" &&
              r.header("strict-transport-security") == "max-age=31536000; includeSubDomains" &&
              r.header("referrer-policy") == "no-referrer" && r.header("x-xss-protection") == "0" &&
              r.header("cross-origin-opener-policy") == "same-origin",
          "helmet: default headers");
    r = call(Get, "/helmet-nocsp");
    CHECK(r.header("content-security-policy").empty() && r.header("strict-transport-security").empty() &&
              r.header("x-frame-options") == "DENY",
          "helmet: options");

    call(Get, "/rl-reset");
    r = call(Get, "/rl");
    CHECK(r.status == 200 && r.header("ratelimit-limit") == "3" && r.header("ratelimit-remaining") == "2" &&
              r.header("x-ratelimit-limit") == "3" && r.header("ratelimit-policy") == "3;w=60",
          "rateLimit: headers");
    call(Get, "/rl");
    call(Get, "/rl");
    r = call(Get, "/rl");
    CHECK(r.status == 429 && r.json()["error"].asString() == "Too many requests, please try again later." &&
              !r.header("retry-after").empty() && r.header("ratelimit-remaining") == "0",
          "rateLimit: 4th request -> 429 + Retry-After");
    call(Get, "/rl-reset");
    r = call(Get, "/rl");
    CHECK(r.status == 200, "rateLimit: store reset");
    bool okRun = true;
    for (int i = 0; i < 4; i++) okRun = okRun && call(Get, "/rl-login?ok=1").status == 200;
    CHECK(okRun, "rateLimit: successful requests not counted");
    call(Get, "/rl-login?ok=0");
    call(Get, "/rl-login?ok=0");
    r = call(Get, "/rl-login?ok=1");
    CHECK(r.status == 429, "rateLimit: failures counted");

    auto upload = [](const std::string &path, std::vector<drogon::UploadFile> files,
                     std::vector<std::pair<std::string, std::string>> params = {}) {
        auto req = HttpRequest::newFileUploadRequest(files);
        req->setPath(path);
        req->setMethod(Post);
        for (auto &[k, v] : params) req->setParameter(k, v);
        auto [rr, resp] = gClient->sendRequest(req, 10.0);
        gLast = resp;
        Resp out;
        if (rr == ReqResult::Ok && resp) {
            out.status = resp->statusCode();
            out.body = std::string(resp->body());
            out.raw = resp;
        }
        return out;
    };
    r = upload("/up/single", {drogon::UploadFile("upload_src/note.txt", "note.txt", "avatar")}, {{"title", "Hello"}});
    j = r.json();
    CHECK(r.status == 200 && j["field"].asString() == "avatar" && j["name"].asString() == "note.txt" &&
              j["size"].asInt() == 12 && j["content"].asString() == "hello upload" && j["mime"].asString() == "text/plain" &&
              j["path"].asString().rfind("uploads_test/", 0) == 0 && j["path"].asString().size() == 13 + 32 + 4 &&
              j["title"].asString() == "Hello",
          "upload.single: disk storage + fields");
    r = upload("/up/single", {drogon::UploadFile("upload_src/big.bin", "big.bin", "avatar")});
    CHECK(r.status == 413 && r.json()["error"].asString() == "File too large", "upload: LIMIT_FILE_SIZE -> 413");
    r = upload("/up/single", {drogon::UploadFile("upload_src/note.txt", "note.txt", "wrong")});
    CHECK(r.status == 400 && r.json()["error"].asString() == "Unexpected field" && r.json()["field"].asString() == "wrong",
          "upload: unexpected field -> 400");
    r = upload("/up/array", {drogon::UploadFile("upload_src/note.txt", "a.txt", "photos"),
                             drogon::UploadFile("upload_src/note.txt", "b.txt", "photos")});
    CHECK(r.status == 200 && r.json()["count"].asInt() == 2, "upload.array");
    r = upload("/up/array", {drogon::UploadFile("upload_src/note.txt", "a.txt", "photos"),
                             drogon::UploadFile("upload_src/note.txt", "b.txt", "photos"),
                             drogon::UploadFile("upload_src/note.txt", "c.txt", "photos")});
    CHECK(r.status == 400, "upload.array: maxCount");
    r = upload("/up/mem", {drogon::UploadFile("upload_src/pic.png", "pic.png", "x"),
                           drogon::UploadFile("upload_src/note.txt", "note.txt", "y")});
    CHECK(r.status == 200 && r.json()["kept"].size() == 1 && r.json()["kept"][0].asString() == "pic.png:8",
          "upload: memory storage + fileFilter");
    r = upload("/up/none", {drogon::UploadFile("upload_src/note.txt", "note.txt", "f")});
    CHECK(r.status == 400, "upload.none: files rejected");
    r = call(Post, "/up/none", R"({"a":1})");
    CHECK(r.status == 200, "upload: non-multipart passes through");

    // WebSocket upgrades pass through Express++ to Drogon's WebSocket controllers
    {
        auto wsc = drogon::WebSocketClient::newWebSocketClient("ws://127.0.0.1:" + std::to_string(PORT));
        auto wsReq = drogon::HttpRequest::newHttpRequest();
        wsReq->setPath("/ws-echo");
        auto got = std::make_shared<std::promise<std::string>>();
        auto fut = got->get_future();
        auto once = std::make_shared<std::atomic<bool>>(false);
        wsc->setMessageHandler([got, once](const std::string &m, const drogon::WebSocketClientPtr &, const drogon::WebSocketMessageType &) {
            if (!once->exchange(true)) got->set_value(m);
        });
        wsc->connectToServer(wsReq, [got, once](drogon::ReqResult rr, const drogon::HttpResponsePtr &, const drogon::WebSocketClientPtr &c) {
            if (rr == drogon::ReqResult::Ok)
                c->getConnection()->send("hi");
            else if (!once->exchange(true))
                got->set_value("connect failed");
        });
        std::string reply = fut.wait_for(std::chrono::seconds(5)) == std::future_status::ready ? fut.get() : "timeout";
        CHECK(reply == "echo:hi", "WebSocket upgrade reaches Drogon's WS controller (not a 404)");
        wsc->stop();
    }

#ifdef EXPRESSPP_TEST_NEVLA
    r = call(Post, "/db/users", R"({"name":"Asha","age":21})");
    CHECK(r.status == 201 && r.json()["name"].asString() == "Asha" && r.json().isMember("_id"), "nevla create");
    std::string id = r.json()["_id"].asString();
    r = call(Get, "/db/users/" + id);
    CHECK(r.status == 200 && r.json()["age"].asInt() == 21, "nevla findById");
    r = call(Get, "/db/users/not-an-id");
    CHECK(r.status == 404 && r.json()["error"].asString() == "User not found", "unknown id -> 404");
    r = call(Post, "/db/users", R"({"age":-4})");
    j = r.json();
    CHECK(r.status == 400 && j["errors"].isMember("name") && j["errors"].isMember("age"), "nevla ValidationError -> 400");
    r = call(Post, "/db/users", R"({"name": oops})");
    CHECK(r.status == 400 && j.isMember("error"), "bad JSON body -> 400");

    // many concurrent coroutine requests: every handler must resume on its own IO loop
    const int N = 300;
    std::atomic<int> ok{0}, done{0};
    for (int i = 0; i < N; i++) {
        auto req = HttpRequest::newHttpRequest();
        req->setMethod(Post);
        req->setPath("/db/users");
        req->setContentTypeCode(CT_APPLICATION_JSON);
        req->setBody(nevla::obj("name", "u" + std::to_string(i), "age", i % 50).toJson());
        gClient->sendRequest(req, [&](ReqResult rr, const HttpResponsePtr &resp) {
            if (rr == ReqResult::Ok && resp->statusCode() == k201Created) ok++;
            done++;
        });
    }
    for (int t = 0; t < 600 && done < N; t++) std::this_thread::sleep_for(std::chrono::milliseconds(20));
    CHECK(ok == N, "300 concurrent coroutine requests");
    int expected = 1;  // Asha (21)
    for (int i = 0; i < N; i++)
        if (i % 50 >= 18) expected++;
    r = call(Get, "/db/users/count?minAge=18");
    CHECK(r.json()["count"].asInt() == expected, "count after concurrent inserts");
    CHECK(gWrongThread == 0, "every co_await resumed on its IO loop");
#endif

    // logger saw every request that got a response through Express++
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    CHECK(gLogLines >= 60, "logger wrote a line per request");
}

static void writeFile(const std::string &path, const std::string &text) {
    std::filesystem::create_directories(std::filesystem::path(path).parent_path());
    std::ofstream(path) << text;
}

static void makeFixtures() {
    writeFile("public_test/a.txt", "hello file\n");
    writeFile("public_test/index.html", "<p>index</p>\n");
    writeFile("public_test/page.html", "<p>page</p>\n");
    writeFile("public_test/.secret", "TOP SECRET\n");
    writeFile("public_test/sub/b.css", "body{}\n");
    writeFile("views_test/header.html", "<h1>{{site}}</h1>\n");
    writeFile("views_test/page.html",
              "{{> header}}Hi {{name}} {{{raw}}} {{#items}}[{{.}}]{{/items}} {{^none}}empty{{/none}} {{user.city}} {{who}}\n");
    writeFile("views_test/thing.txt", "unused");
    writeFile("upload_src/note.txt", "hello upload");
    writeFile("upload_src/big.bin", std::string(2048, 'x'));
    writeFile("upload_src/pic.png", "PNGDATA!");
    std::filesystem::remove_all("uploads_test");
}

int main() {
    makeFixtures();
    testEnv();
    testJson();
    testSecurityUnits();
    testCrypto();

    express::App app;
    buildApp(app);
    app.threads(4);

    std::thread checker([&] {
        while (!drogon::app().isRunning()) std::this_thread::sleep_for(std::chrono::milliseconds(20));
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        gClient = HttpClient::newHttpClient("http://127.0.0.1:" + std::to_string(PORT));
        runChecks();
        gClient.reset();
        express::App::stop();
    });
    app.listen(PORT, "127.0.0.1");
    checker.join();
#ifdef EXPRESSPP_TEST_NEVLA
    nevla::disconnect();
#endif
    std::cout << gPass << " passed, " << gFail << " failed\n";
    return gFail == 0 ? 0 : 1;
}
