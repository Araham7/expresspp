# Express++: Drogon pe Express jaisa development

Express++ ek C++20 library hai jo [Drogon](https://github.com/drogonframework/drogon) ke upar Express.js jaisa API deti hai. Isse server Node jaisa aasaan likha jaata hai, aur speed Drogon wali milti hai. Database ke liye ye [nevla](../nevla) (Mongoose for C++) ke saath seedha kaam karti hai.

```cpp
#include <express/express.hpp>

int main() {
    express::env::load();
    express::App app;
    app.use(express::logger("dev"));
    app.use(express::cors());
    app.use(express::json());
    app.use(express::serveStatic("public"));

    app.get("/users/:id", [](express::Req req, express::Res res) {
        res.json(express::obj("id", req.param("id")));
    });

    app.listen(3000);
}
```

- **Naam:** brand **Express++**, C++ namespace **`express::`**, package/repo **`expresspp`**. C++ ke naamon me `+` nahi aa sakta, isliye code me ye naam hain.
- **Version 0.4.2.** Badlaav [CHANGELOG.md](CHANGELOG.md) me hain.

## Contents

- [Express → Express++ cheat sheet](#express--express-cheat-sheet)
- [Build aur install](#build-aur-install)
- [Routing](#routing)
- [Middleware](#middleware)
- [Error handling](#error-handling)
- [co_await](#co_await-async-handlers)
- [Request](#request-req)
- [Response](#response-res)
- [Templates (res.render)](#templates-resrender)
- [Static files](#static-files-expressservestatic)
- [Body: json() aur limits](#body-expressjson-aur-limits)
- [App settings](#app-settings)
- [Security: jwt, hash, validate, helmet, rateLimit, upload, cookieParser, crypto](#security-packages)
- [offload(): blocking kaam bina IO loop roke](#offload)
- [Packages: env (do tareeke), logger, cors, nevla](#packages)
- [Tests aur examples](#tests)
- [Roadmap aur limits](#roadmap)

## Express → Express++ cheat sheet

| Express (Node) | Express++ (C++) |
|---|---|
| `const app = express()` | `express::App app;` |
| `app.use(mw)` / `app.use('/api', router)` | `app.use(mw);` / `app.use("/api", router);` |
| `express.Router()` | `express::Router r;` |
| `app.get('/u/:id', (req, res) => ...)` | `app.get("/u/:id", [](express::Req req, express::Res res) { ... });` |
| `app.delete(...)` | `app.del(...)` (`delete` C++ ka keyword hai) |
| `app.route('/b').get(f).post(g)` | `app.route("/b").get(f).post(g);` |
| `'/u/:id?'`, `'/u/:id(\\d+)'`, `/^\/v(\d+)$/` | `"/u/:id?"`, `"/u/:id(\\d+)"`, `std::regex("^/v(\\d+)$")` |
| `app.param('id', (req, res, next, id) => ...)` | `app.param("id", [](express::Req, express::Res, express::Next next, std::string id) { ... });` |
| `(req, res, next) => { next() }` | `[](express::Req req, express::Res res, express::Next next) { next(); }` |
| `(err, req, res, next) => ...` | `[](express::Error err, express::Req req, express::Res res, express::Next next) { ... }` |
| `next(err)` | `next(err.ptr())`, `next.error("msg", 401)`, ya bas `throw` |
| `async (req, res) => { await ... }` | `[](express::Req req, express::Res res) -> express::Task<> { co_await ...; }` |
| `req.params.id` / `req.query.page` | `req.param("id")` / `req.query("page", "1")` |
| `req.body` (json) | `req.json()` (`Json::Value`) / `express::nv::body(req)` (`nevla::Value`) |
| `req.get('x-token')` / `req.cookies.sid` | `req.header("x-token")` / `req.cookie("sid")` (ya `req.cookies()["sid"]`) |
| `app.use(cookieParser("secret"))`, `req.signedCookies.uid` | `app.use(express::cookieParser("secret"));`, `req.signedCookie("uid")` |
| `res.cookie("uid", v, {signed: true})` | `res.cookie("uid", v, {.sign = true});` |
| `crypto.randomBytes(32).toString("hex")` | `express::crypto::randomBytes(32).toString("hex")` |
| `crypto.createHash("sha256").update(x).digest("hex")` | `express::crypto::createHash("sha256").update(x).digest("hex")` |
| `req.ip`, `req.ips`, `req.hostname`, `req.protocol`, `req.secure`, `req.xhr` | `req.ip()`, `req.ips()`, `req.hostname()`, `req.protocol()`, `req.secure()`, `req.xhr()` |
| `req.accepts('json', 'html')` / `req.is('json')` | `req.accepts("json", "html")` / `req.is("json")` |
| `req.user = u` | `req.set("user", u)` → `req.get<User>("user")` |
| `req.baseUrl`, `req.path`, `req.originalUrl` | `req.baseUrl()`, `req.path()`, `req.originalUrl()` |
| `res.status(201).json({...})` | `res.status(201).json(express::obj(...));` |
| `res.send('hi')` / `res.sendStatus(204)` | `res.send("hi");` / `res.sendStatus(204);` |
| `res.set()`, `res.get()`, `res.type()`, `res.append()` | `res.set()`, `res.get()`, `res.type()`, `res.append()` |
| `res.location()`, `res.vary()` | `res.location()`, `res.vary()` |
| `res.cookie()`, `res.clearCookie()` | `res.cookie("sid", v, {.httpOnly = true});`, `res.clearCookie("sid");` |
| `res.redirect('/login')` | `res.redirect("/login");` |
| `res.sendFile(p)` / `res.download(p, name)` / `res.attachment(name)` | same naam |
| `res.render('page', data)` | `res.render("page", express::obj(...));` |
| `res.format({html: f, json: g})` | `res.format({{"html", f}, {"json", g}});` |
| `res.locals` / `app.locals` | `res.locals()` / `app.locals()` |
| `app.set('trust proxy', true)` | `app.set("trust proxy", true);` |
| `app.engine('mst', fn)` | `app.engine(".mst", fn);` |
| `express.static('public')` | `express::serveStatic("public")` (`static` C++ ka keyword hai) |
| `express.json({limit: '1mb'})` | `express::json({.limit = "1mb"})` |
| `app.listen(3000)` | `app.listen(3000);` |
| `dotenv` | `express::env` |
| `morgan('dev')` | `express::logger("dev")` |
| `cors({origin, credentials})` | `express::cors({.origins = {...}, .credentials = true})` |
| `process.env.PORT \|\| 3000` | `express::process.env["PORT"] \|\| 3000` **ya** `express::env::getInt("PORT", 3000)` ([dono tareeke](#environment-variables-do-tareeke)) |
| `jsonwebtoken` (`jwt.sign/verify/decode`) | `express::jwt::sign/verify/decode` |
| `express-jwt` | `express::auth({.secret = s})` → `express::jwt::user(req)` |
| `bcrypt` / `argon2` | `express::hash::makeAsync/verifyAsync` (argon2id; bcrypt hashes bhi verify hote hain) |
| `joi` / `zod` / `express-validator` | `express::v::object({...})` + `express::validate({.body = schema})` |
| `helmet()` | `express::helmet()` |
| `express-rate-limit` | `express::rateLimit({.windowMs = 60000, .limit = 60})` |
| `multer` (`upload.single/array/fields/any/none`) | `express::upload({...}).single("avatar")` → `express::file(req)` |
| `app.disable('x-powered-by')` | `app.disable("x-powered-by")` |
| `mongoose` | `nevla` + `express::nv::use(app, uri)` |

## Build aur install

Zaroorat: GCC 11+ / Clang 14+ (C++20), CMake 3.16+, Drogon 1.8+.

```bash
# Ubuntu 24.04
sudo apt install build-essential cmake ninja-build pkg-config libdrogon-dev libjsoncpp-dev uuid-dev \
                 libpq-dev libmariadb-dev libsqlite3-dev libhiredis-dev libyaml-cpp-dev \
                 libssl-dev libsodium-dev libcrypt-dev
# (Ubuntu ka Drogon package in DB client libraries ke bina CMake me "not found" bolta hai)

cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
sudo cmake --install build
```

Agar nevla install hai (`find_package(nevla)` mil jaata hai), to `expresspp::nevla` target aur nevla wale tests/example apne aap ban jaate hain.

Apne project me:

```cmake
find_package(expresspp 0.3 CONFIG REQUIRED)
target_link_libraries(myserver PRIVATE expresspp::expresspp)  # sab kuch
target_link_libraries(myserver PRIVATE expresspp::nevla)      # nevla glue (optional)
```

Har package ka apna CMake target bhi hai:
- `expresspp::core`: App, Router, Req, Res, templates, `serveStatic`, `json()`
- `expresspp::env`: iske liye Drogon nahi chahiye
- `expresspp::logger`
- `expresspp::cors`
- `expresspp::security`: jwt, auth, hash, validate, helmet, rateLimit, upload, offload (OpenSSL, libsodium, libcrypt chahiye)

## Routing

```cpp
app.get("/", handler);
app.post("/users", validate, create);            // route-level middleware: jitne chahe
app.all("/ping", handler);                       // har method

app.route("/books")                              // ek path, kai methods
   .get(listBooks)
   .post(createBook)
   .del(deleteBook);
```

### Path patterns

| Pattern | Match karta hai | Params |
|---|---|---|
| `"/users/:id"` | `/users/42` | `id = "42"` |
| `"/users/:id?"` | `/users` aur `/users/42` | na ho to `req.param("id", "default")` |
| `"/users/:id(\\d+)"` | sirf `/users/42`, `/users/abc` nahi | `id = "42"` |
| `"/files/*"` | `/files/a/b.txt` | `* = "a/b.txt"` |
| `std::regex("^/v(\\d+)/status$")` | `/v3/status` | `0 = "3"` (regex groups: `"0"`, `"1"`, ...) |

- `HEAD` request apne aap `GET` route se match hoti hai.
- Layers usi order me chalti hain jisme add ki gayi, Express ki tarah. Isliye `"/num/:id(\\d+)"` ke baad `"/num/:slug"` lagao to digits wala pehle try hota hai.
- WebSocket upgrade requests Express++ ke routes me nahi aati, wo seedha Drogon ke WebSocket controllers (jaise **socketiopp**) ke paas jaati hain.
- Koi route match na ho to `404 Cannot GET /path` jaata hai. Ise badalne ke liye `app.notFound(fn)` lagao. Drogon ke apne controllers ya static files chalani hain to `app.passUnmatchedToDrogon()` lagao.

### Router

```cpp
express::Router users;
users.use(auth);                                 // sirf is router ke liye
users.get("/", list);
users.get("/:id", show);
app.use("/api/:ver/users", users);               // GET /api/v1/users/42
// handler ke andar: req.param("ver") == "v1", req.param("id") == "42",
//                  req.baseUrl() == "/api/v1/users", req.path() == "/42"
```

Router ke andar Router bhi lag sakta hai. Bahar wale router ke params andar bhi milte hain.

### app.param()

```cpp
app.param("userId", [](express::Req req, express::Res, express::Next next, std::string id) -> express::Task<> {
    auto user = co_await User().findById(id).leanAsync();
    if (!user) throw express::HttpError(404, "User not found");
    req.set("user", *user);
    next();
});
app.get("/users/:userId", show);
app.get("/users/:userId/posts", posts);
```

- Ye callback har request me ek hi baar chalta hai, chahe kitne bhi routes match ho jaayein.
- Ye sirf usi router ke routes pe lagta hai jisme add kiya gaya ho, Express ki tarah.

## Middleware

```cpp
auto auth = [](express::Req req, express::Res res, express::Next next) {
    if (req.header("authorization").empty()) throw express::HttpError(401, "Unauthorized");
    req.set<std::string>("user", "asha");
    next();
};
app.use(auth);                    // poori app pe
app.use("/admin", auth);          // sirf /admin... pe; andar req.path() "/admin" ke baad ka hissa hai
app.get("/x", auth, handler);     // ek route pe
```

- `next()` baad me bhi call ho sakta hai, kisi doosre thread se bhi.
- `next()` do baar call karne pe doosri baar ignore hoti hai (warning log hoti hai).
- `Req` aur `Res` chhote handles hain, inhe value se copy karke lambda me rakh lo.

## Error handling

Error aane pe Express++ bilkul Express ki tarah chalta hai. Normal middleware skip hote jaate hain, aur sirf error middleware chalta hai.

```cpp
express::Router api;
api.get("/fail", [](express::Req, express::Res) { throw std::runtime_error("broke"); });

// error middleware: 4 arguments
api.use([](express::Error err, express::Req req, express::Res res, express::Next next) {
    if (err.is<express::HttpError>()) return next(err.ptr());      // aage bhejo
    res.status(500).json(express::obj("apiError", err.message()));
});
app.use("/api", api);
```

- `Error` ke methods: `err.message()`, `err.status()` (HttpError ho to uska status, warna 500), `err.is<T>()`, `err.as<T>()` (pointer), `err.rethrow()`, `err.ptr()`.
- `next(err.ptr())`: error aage jaata hai, agle error middleware ya parent router tak.
- `next()` bina error ke: error khatam, agle **normal** layer pe chala jaata hai (recover).
- Error middleware ke andar `throw` kiya to wo naya error ban jaata hai.
- Error middleware `Task<>` (coroutine) bhi ho sakta hai.

Jo error kisi ne handle nahi kiya, wo app-level handlers pe aata hai:

```cpp
throw express::HttpError(404, "User not found");      // -> 404 {"error": "User not found"}
throw std::runtime_error("x");                        // -> 500 {"error": "x"}  (production me "Internal Server Error")

app.onError<MyError>([](const MyError &e, express::Req, express::Res res) {
    res.status(e.code).json(express::obj("error", e.what()));
});
```

App ke `onError` handlers ulte order me try hote hain, yaani jo baad me add hua wo pehle chalta hai. Production mode `EXPRESS_ENV=production` ya `NODE_ENV=production` se on hota hai.

## co_await (async handlers)

Return type `express::Task<>` (yaani `drogon::Task<void>`) rakho, phir andar `co_await` chalega:

```cpp
app.get("/users", [](express::Req req, express::Res res) -> express::Task<> {
    auto rows = co_await User().find("age >= 18").leanAsync();                 // nevla
    auto r2   = co_await drogon::app().getDbClient()->execSqlCoro("select 1"); // Drogon ORM
    res.json(express::obj("count", static_cast<int64_t>(rows.size())));
});
```

- Middleware, error middleware aur `app.param()`, teeno coroutine ho sakte hain.
- `co_await` ke baad aaya exception bhi error middleware tak pahunchta hai.

> **GCC 13 tip:** `co_await` wali line me `{{...}}` braces mat likho, GCC 13 crash karta hai. `express::obj()` / `express::arr()` aur `nevla::obj()` / `nevla::arr()` braces-free hain, wahi use karo.

## Request (`req`)

| Method | Kya deta hai |
|---|---|
| `method()`, `path()`, `baseUrl()`, `originalUrl()` | `"GET"`, router ke andar ka path, mount path, poora path |
| `param("id", def)`, `params()` | route params |
| `query("page", def)`, `queries()` | query string aur urlencoded form fields |
| `header("x")`, `cookie("sid")` | header (case-insensitive), cookie |
| `body()`, `json()` | raw body, parsed JSON |
| `ip()`, `ips()` | client IP. `"trust proxy"` on ho to `X-Forwarded-For` se |
| `hostname()` | `Host` (ya trust proxy ke saath `X-Forwarded-Host`), port ke bina |
| `protocol()`, `secure()` | `"http"`/`"https"` (trust proxy ke saath `X-Forwarded-Proto`) |
| `xhr()` | `X-Requested-With: XMLHttpRequest` |
| `accepts("json", "html")` | client ka best type (`Accept` header, q-values ke saath), `""` = koi nahi |
| `is("json")` | body ka Content-Type match karta hai? (`"json"`, `"html"`, `"text/*"`, `"+json"`) |
| `set(k, v)`, `get<T>(k)`, `has(k)` | request-scoped data |
| `raw()` | `drogon::HttpRequestPtr` |

## Response (`res`)

```cpp
res.status(201).json(express::obj("id", 7, "tags", express::arr("a", "b")));
res.json(nevlaDoc.toJSON());             // jis cheez me .toJson() hai, wo seedha json() me ja sakti hai
res.type("text").send("ok");             // "json" / "html" / "text" / "csv" / poora mime type
res.set("X-A", "1").append("Link", "<a>").append("Link", "<b>");   // Link: <a>, <b>
res.vary("Accept");
res.location("/users/7").sendStatus(201);
res.cookie("sid", token, {.httpOnly = true, .secure = true, .maxAge = 3600, .sameSite = "Strict"});
res.clearCookie("sid");
res.redirect("/login");                  // ya redirect(url, 301)
res.sendFile("public/report.pdf");       // file na ho to 404
res.download("files/r.pdf", "Report.pdf");          // attachment + file
res.attachment("data.csv").type("csv").send(csv);   // sirf Content-Disposition
res.render("profile", express::obj("name", "Asha"));
res.sendRaw(drogonResponse);             // apna drogon::HttpResponsePtr (streaming, custom)
```

### res.format(): content negotiation

```cpp
res.format({
    {"html", [res]() mutable { res.render("user", data); }},
    {"json", [res]() mutable { res.json(data); }},
    {"default", [res]() mutable { res.sendStatus(406); }}   // optional
});
```

- `Accept` header ke hisaab se sahi handler chalta hai, aur `Vary: Accept` apne aap lag jaata hai.
- Koi type match na ho aur `default` bhi na ho to 406 jaata hai.

Do baatein yaad rakho:
- Response **kisi bhi thread se** bheja ja sakta hai. Express++ use request ke apne IO loop pe wapas bhejta hai.
- Do baar send karne pe doosri baar ignore hoti hai.

## Templates (`res.render`)

Built-in engine Mustache syntax use karta hai, aur iske liye koi extra library nahi chahiye:

```cpp
app.set("views", "views");               // default "views"
app.set("view engine", "html");          // default ".html"
app.locals()["site"] = "My Site";        // har render me milega

app.get("/u/:name", [](express::Req req, express::Res res) {
    res.locals()["year"] = 2026;
    res.render("profile", express::obj("name", req.param("name"), "skills", express::arr("C++", "IoT")));
});
```

`views/profile.html`:
```html
{{> header}}
<h1>{{name}}</h1>                  <!-- HTML-escaped -->
{{{rawHtml}}}                      <!-- escape nahi hota -->
<ul>{{#skills}}<li>{{.}}</li>{{/skills}}</ul>
{{^skills}}<p>No skills yet</p>{{/skills}}
<p>{{user.address.city}} · {{site}} · {{year}}</p>
{{! ye comment hai }}
```

- **Data ka order:** `app.locals()` → `res.locals()` → `render()` ka data. Baad wala pehle wale ko override karta hai.
- **Cache:** production me templates cache hote hain. Manually on/off ke liye `app.set("view cache", true)` lagao.
- **Apna engine:** `app.engine(".mst", [](const std::string &file, const express::Json &data) { return html; });`
- **Bina response ke:** `express::renderTemplate(text, data)` se seedha string milti hai.

## Static files (`express::serveStatic`)

```cpp
app.use(express::serveStatic("public"));
app.use("/assets", express::serveStatic("dist/assets", {.maxAge = 31536000, .immutable = true}));
app.use(express::serveStatic("dist", {.spaFallback = "index.html"}));      // React / Vite app
app.use(express::serveStatic("site", {.extensions = {".html"}}));          // /about -> about.html
```

Options:
- `index`: default `"index.html"`
- `maxAge`, `immutable`: Cache-Control
- `dotfiles`: `"ignore"` (default, jaise file hai hi nahi), `"deny"` (403), ya `"allow"`
- `fallthrough`: default `true`. File na mile to `next()`, warna 404.
- `extensions`
- `spaFallback`: sirf HTML maangne wali requests ke liye
- `etag`, `lastModified`

Ye bhi sambhalta hai:
- Sirf GET/HEAD requests serve hoti hain.
- `ETag`/`Last-Modified` header lagte hain aur 304 support hai.
- `..` aur symlinks se root folder ke bahar nahi ja sakte.
- `.env`, `.git` jaisi dotfiles default me chhupi rehti hain.

## Body: `express::json()` aur limits

Drogon JSON aur urlencoded body khud parse kar leta hai, isliye `req.json()` aur `req.query("field")` bina kuch lagaye chalte hain. `express::json()` iske upar wo checks deta hai jo Express me hote hain:

```cpp
app.set("body limit", "10mb");                       // sabse badi body jo server lega (Drogon default: 1mb)
app.use(express::json());                            // galat JSON -> 400, badi body -> 413
app.use(express::json({.limit = "100kb", .strict = true, .type = "json"}));
app.post("/upload", express::bodyLimit("5mb"), handler);   // sirf ek route pe limit
```

- `strict`: top level pe sirf object ya array chalega.
- `type = "json"` wale `+json` types (jaise `application/vnd.api+json`) bhi pakad leta hai.

## App settings

| Setting | Matlab |
|---|---|
| `app.set("trust proxy", true)` | `req.ip()/protocol()/hostname()` ke liye `X-Forwarded-*` headers use karo (Nginx ke peeche ho tab) |
| `app.set("views", "views")` | `res.render()` ka folder |
| `app.set("view engine", "html")` | template ka default extension |
| `app.set("view cache", true)` | templates cache karo (default: sirf production me) |
| `app.set("body limit", "10mb")` | sabse badi request body |
| `app.set("koi bhi", "value")` → `app.setting("koi bhi")` | apni settings |
| `app.enable(k)`, `app.disable(k)`, `app.enabled(k)` | true/false wali settings |
| `app.disable("x-powered-by")` | `Server: drogon/x.y` header band (helmet jaisa) |
| `app.threads(4)` | IO threads (default: jitne CPU cores hain) |

Express me setting padhne ke liye `app.get(key)` hota hai. Yahan `get` routing ke liye hai, isliye `app.setting(key)` likhte hain.

## Security packages

Login/signup wala poora example `examples/04_auth_api.cc` me hai.

### `express::jwt` + `express::auth()` (jsonwebtoken + express-jwt)

```cpp
std::string secret = express::env::require("JWT_SECRET");    // zaroori: na ho to server start hi nahi hoga
// ya Node style (missing ho to "" milega, isliye secrets ke liye require() behtar hai):
// std::string secret = express::process.env["JWT_SECRET"];

app.post("/login", [secret](express::Req req, express::Res res) {
    // ... password check ...
    auto token = express::jwt::sign(express::obj("id", 7, "role", "admin"), secret, {.expiresIn = "7d"});
    res.json(express::obj("token", token));
});

app.use("/api", express::auth({.secret = secret}));          // "Authorization: Bearer <token>" zaroori
app.get("/api/me", [](express::Req req, express::Res res) {
    res.json(express::jwt::user(req));                        // {"id":7,"role":"admin","iat":...,"exp":...}
});
```

- **Algorithms:** HS256 (default), HS384, HS512, aur RS256 (PEM private key se sign, public key se verify). `none` hamesha reject hota hai.
- **SignOptions:** `expiresIn`, `notBefore` (`"30s"`, `"15m"`, `"12h"`, `"7d"`, `"2w"`, `"1y"`), `issuer`, `subject`, `audience`, `jwtid`, `keyid`, `noTimestamp`.
- **VerifyOptions:** `algorithms` (allow-list), `issuer`, `audience`, `subject`, `clockTolerance`, `maxAge`, `ignoreExpiration`.
- **Errors** jsonwebtoken jaise hain: `jwt::TokenExpiredError` ("jwt expired"), `jwt::NotBeforeError`, aur `jwt::Error` ("invalid signature", "jwt malformed", ...). Sab `HttpError(401)` hain, isliye pakde na jaayein to apne aap 401 response ban jaate hain.
- **AuthOptions:**
  - `cookie = "token"`: cookie me bhi token dhoondo
  - `credentialsRequired = false`: token na ho to bhi aage jao
  - `property`: `req` pe kis naam se payload rakhna hai
  - `issuer`/`audience`
  - `getToken`: apna tareeka token nikalne ka
  - `isRevoked`: logout list check karne ke liye
- **Node ke saath compatible:** jwt.io ka example token yahan verify hota hai. Node ke `jsonwebtoken` ke saath dono taraf check kiya gaya hai, wahan ke tokens yahan verify hote hain aur yahan ke wahan.

### `express::hash` (argon2 / bcrypt)

```cpp
app.post("/signup", [](express::Req req, express::Res res) -> express::Task<> {
    std::string h = co_await express::hash::makeAsync(req.json()["password"].asString());
    // save h ...
});
bool ok = co_await express::hash::verifyAsync(password, savedHash);
```

- **Default argon2id** hai (libsodium), standard `$argon2id$v=19$m=65536,t=3,p=1$...` string me. Node ke `argon2` package ke saath dono taraf check kiya gaya hai.
- **bcrypt** (`$2a$`/`$2b$`/`$2y$`) bhi verify hota hai, isliye Node ke `bcrypt`/`bcryptjs` se bane purane hashes chalte rahenge. Naya bcrypt hash: `express::hash::bcrypt(pw, 10)`.
- `needsRehash(h)`: login ke waqt purane/kamzor hash ko upgrade karne ke liye.
- Hashing jaan-boojh ke ~50–100 ms leta hai. **Handler me hamesha `*Async` version use karo**, wo worker pool pe chalta hai aur IO loop nahi rukta.

### `express::validate` (joi / zod)

```cpp
namespace v = express::v;
auto signup = v::object({
    {"email",    v::string().trim().lowercase().email().required()},
    {"password", v::string().min(8).max(72).required()},
    {"age",      v::integer().min(13)},
    {"role",     v::string().oneOf({"user", "admin"}).defaultTo("user")},
    {"tags",     v::array(v::string().max(20)).max(5)},
});

app.post("/signup", express::validate({.body = signup}), [](express::Req req, express::Res res) {
    const express::Json &body = express::validated(req);    // saaf data: trim, defaults, unknown keys hata diye
});
app.get("/items", express::validate({.query = v::object({{"page", v::integer().min(1).defaultTo(1)}})}), list);
```

Galat data pe response:
```json
{"error": "Validation failed", "errors": {"email": "must be a valid email", "tags.1": "must be at most 20 characters"}}
```

**Rules:**
- **Sab pe:** `required()`, `optional()`, `nullable()`, `defaultTo(x)`, `oneOf({...})`, `custom(fn)`, `message("...")`
- **string:** `min`, `max`, `length`, `pattern(regex, "name")`, `email()`, `url()`, `uuid()`, `trim()`, `lowercase()`, `uppercase()`, `nonEmpty()`
- **number / integer:** `min`, `max`, `positive()`, `negative()`
- **array(items):** `min`, `max`, `length`, `unique()`
- **object(fields):** unknown keys default me hata di jaati hain. `strict()` lagao to unknown key pe error, `passthrough()` lagao to unknown keys rakh li jaati hain.
- **boolean(), any()**

Kuch aur baatein:
- `query`/`params` hamesha strings hote hain. Unme `"2"` apne aap `2` ban jaata hai, `"true"` → `true`, aur `"a,b"` → `["a","b"]`.
- Bina middleware ke seedha bhi chala sakte ho: `auto r = schema.validate(json); r.ok / r.errors / r.value`

### `express::helmet`

```cpp
app.use(express::helmet());
app.use(express::helmet({.contentSecurityPolicy = "default-src 'self'; img-src * data:", .frameguard = "DENY"}));
app.disable("x-powered-by");     // Drogon ka "Server" header bhi band
```

helmet ke saare default headers lagte hain: CSP, COOP, CORP, Origin-Agent-Cluster, Referrer-Policy, HSTS, nosniff, X-DNS-Prefetch-Control, X-Download-Options, X-Frame-Options, X-Permitted-Cross-Domain-Policies, `X-XSS-Protection: 0`. Kisi header ko band karna ho to option me `""` ya `false` do.

### `express::rateLimit` (express-rate-limit)

```cpp
app.use(express::rateLimit({.windowMs = 15 * 60 * 1000, .limit = 100}));
app.post("/login", express::rateLimit({.windowMs = 60000, .limit = 5, .skipSuccessfulRequests = true}), login);
```

- Limit paar hone pe **429** + `Retry-After` jaata hai. Headers: `RateLimit-Limit/-Remaining/-Reset/-Policy` (aur `legacyHeaders = true` pe `X-RateLimit-*`).
- **Key** default me `req.ip()` hai. Nginx ke peeche ho to `app.set("trust proxy", true)` zaroor lagao, warna sab users ka IP ek hi dikhega. Apna key chahiye to `keyGenerator` do.
- Aur options: `skip`, `handler` (apna 429 response), `message`, `statusCode`.
- **Store:** default `MemoryStore` hai, jo ek process ke liye kaafi hai. Kai servers ho to Redis wala store `RateLimitStore` implement karke do. `limiter.store->reset(key)` se kisi key ka count reset kar sakte ho.

### `express::upload` (multer)

```cpp
auto upload = express::upload({.dest = "uploads", .limits = {.fileSize = "5mb", .files = 5}});

app.post("/avatar", upload.single("avatar"), [](express::Req req, express::Res res) {
    const express::UploadedFile *f = express::file(req);      // req.file
    std::string title = express::fields(req)["title"].asString();   // req.body (text fields)
    res.json(express::obj("path", f->path, "size", f->size));
});
app.post("/photos", upload.array("photos", 8), handler);              // express::files(req)
app.post("/profile", upload.fields({{"avatar", 1}, {"gallery", 8}}), handler);
app.post("/any", upload.any(), handler);
app.post("/form", upload.none(), handler);
```

- **`UploadedFile` ke fields:** `fieldname`, `originalname`, `mimetype`, `size`, `destination`, `filename`, `path`, `buffer`
- **Storage:** disk pe file ka naam random 32 hex + original extension hota hai, aur folder apne aap ban jaata hai. RAM me rakhni ho to `.memory = true` lagao, data `buffer` me milega.
- **`fileFilter`:** `false` return karo to file skip ho jaati hai (multer ke `cb(null, false)` jaisa). Poori request reject karni ho to `throw HttpError`.
- **Errors** multer jaise: 413 "File too large", 413 "Too many files", 400 "Unexpected field" (`{"field": "..."}` ke saath).
- **Bade uploads** ke liye `app.set("body limit", "50mb")` lagao (Drogon default 1 MB hai).

`process.env` (Node style) ka poora tareeka [Environment variables: do tareeke](#environment-variables-do-tareeke) me hai.

### `express::cookieParser` (cookie-parser)

```cpp
app.use(express::cookieParser(express::env::get("COOKIE_SECRET")));
// rotation: naye secret se sign, purane bhi accept
app.use(express::cookieParser({"new-secret", "old-secret"}));

res.cookie("uid", "42", {.httpOnly = true, .sign = true});   // Set-Cookie: uid=s%3A42.<HMAC>
auto uid = req.signedCookie("uid");     // std::optional<std::string>: tamper hua ya nahi hai to std::nullopt
if (!uid) throw express::HttpError(401, "Login again");
auto all  = req.signedCookies();        // saari sahi signed cookies
auto rest = req.cookies();              // baaki (unsigned) cookies, URL-decoded
```

- **Node ke saath same format** (`s:` + value + `.` + base64 HMAC-SHA256, `=` hata ke). Node ki `cookie-parser` / `cookie-signature` se check kiya hai, to ek Node app aur ek Express++ app ek dusre ki signed cookies padh sakte hain.
- `signed` C++ ka keyword hai, isliye option ka naam **`.sign`** hai.
- `.sign = true` bina `cookieParser("secret")` ke → error (500), Express jaisa.
- `res.cookie()` ab har value ko `encodeURIComponent` karta hai aur `req.cookie()` decode karta hai, Express ki tarah.
- Helpers: `express::cookieSign(value, secret)`, `express::cookieUnsign(signed, secret)`.

### `express::crypto` (Node ka `crypto` module)

```cpp
namespace crypto = express::crypto;

std::string token = crypto::randomBytes(32).toString("hex");    // reset-password token
std::string id    = crypto::randomUUID();                         // "3b241101-e2bb-4255-8caf-4136c566a962"
long long otp     = crypto::randomInt(100000, 1000000);          // 6-digit OTP

std::string sha = crypto::createHash("sha256").update("abc").digest("hex");
std::string sig = crypto::createHmac("sha256", secret).update(rawBody).digest("hex");   // webhook signature
bool same = crypto::timingSafeEqual(sig, req.header("x-signature"));

auto key = crypto::scryptSync(password, salt, 32);               // ya pbkdf2Sync(password, salt, 100000, 32, "sha256")

// AES-256-GCM: encrypt
auto iv = crypto::randomBytes(12);
auto c = crypto::createCipheriv("aes-256-gcm", key, iv);
std::string enc = c.update("secret data", "utf8", "hex") + c.final("hex");
auto tag = c.getAuthTag();
// decrypt
auto d = crypto::createDecipheriv("aes-256-gcm", key, iv);
d.setAuthTag(tag);
std::string plain = d.update(enc, "hex", "utf8") + d.final("utf8");   // data badla gaya ho to throw
```

- **Encodings:** `"hex"`, `"base64"`, `"base64url"`, `"utf8"` / `"binary"` / `"latin1"` (raw bytes). `crypto::Buffer::from(text, "base64")` aur `buf.toString("hex")`, Node ke Buffer jaisa.
- **Hashes:** md5, sha1, sha224/256/384/512, sha3-256/512, blake2b512 (OpenSSL jo jaanta hai sab). **Ciphers:** aes-128/192/256-gcm / cbc / ctr, chacha20-poly1305. `setAAD()`, `setAutoPadding()` bhi hain.
- **`update(...) + final(...)` ek line me safe hai.** C++ me `+` ke dono taraf ka order fixed nahi hota, isliye `final()` turant finish nahi karta: uska result use hone par finish hota hai. Node wali line jaisi ki taisi chalti hai. Base64 output ke chunks bhi Node ki tarah jud jaate hain.
- Galat key/IV length, ya tampered data → `express::crypto::Error`.
- Sab Node ke output se match kiya hai (sha512, HMAC, pbkdf2, scrypt, AES-GCM ciphertext + tag, AES-CBC).
- Passwords ke liye `crypto` nahi, **`express::hash`** (argon2id) use karo.

## offload()

Koi bhi blocking kaam ho (image resize, purani blocking library), use worker pool pe bhejo aur `co_await` karo. Itne me IO loop baaki requests sambhalta rehta hai:

```cpp
app.post("/resize", [](express::Req req, express::Res res) -> express::Task<> {
    std::string png = co_await express::offload([body = req.body()] { return resizeImage(body); });
    res.type("png").send(png);
});
```

## Packages

### Environment variables: do tareeke

**Step 1: `.env` file banao** (project folder me, Git me commit mat karna):
```
PORT=3000
CLIENT_URL=http://localhost:5173
DEBUG=true
JWT_SECRET=koi_lamba_random_string
```

**Step 2: load karo**, code ki shuruaat me ek baar (Node ke `require('dotenv').config()` jaisa):
```cpp
express::env::load();          // ./.env padhta hai; pehle se set env vars nahi badalte
```

**Step 3: values padho.** Iske do tareeke hain, aur dono ka result bilkul same hai. Jo pasand ho wo use karo:

| Kaam | Tareeka 1: Node style (`express::process.env`) | Tareeka 2: Express++ style (`express::env`) |
|---|---|---|
| Text, default ke saath | `std::string url = express::process.env["CLIENT_URL"] \|\| "*";` | `std::string url = express::env::get("CLIENT_URL", "*");` |
| Number | `int port = express::process.env["PORT"] \|\| 3000;` | `int port = express::env::getInt("PORT", 3000);` |
| Decimal | `double r = express::process.env["RATE"] \|\| 1.5;` | `double r = express::env::getDouble("RATE", 1.5);` |
| true/false | `bool dbg = express::process.env["DEBUG"] \|\| false;` | `bool dbg = express::env::getBool("DEBUG", false);` |
| Bina default ke | `std::string k = express::process.env["KEY"];` (na ho to `""`) | `std::string k = express::env::get("KEY");` (na ho to `""`) |
| Hai ya nahi? | `express::process.env["KEY"].exists()` | `express::env::has("KEY")` |
| Set karna | `express::process.env["MODE"] = "test";` | `setenv("MODE", "test", 1);` |
| **Zaroori value** (na ho to error) | — (Node me nahi hota) | `std::string s = express::env::require("JWT_SECRET");` |

- **Kaunsa chunein:** Node se aaye ho to tareeka 1 sabse aasaan lagega. Lekin secrets (`JWT_SECRET`, DB password) ke liye hamesha `express::env::require()` use karo. Wo bhool jaane pe server ko khaali secret pe chalne nahi deta.
- **Dono me same rules:** variable missing ho **ya khaali** ho (`PORT=`), dono me default milta hai (Node jaisa). `PORT=abc` jaisi galat value pe dono error dete hain, chupchaap galat value nahi lete.
- `true/false` me ye sab chalta hai: `1/true/yes/on` aur `0/false/no/off`.
- Andar se tareeka 1, tareeka 2 ko hi call karta hai. Code ek hi hai, bas likhne ka style alag hai.

`.env` file me ye sab support hota hai: `export KEY=v`, `"double \n quotes"`, `'single (literal)'`, `${VAR}` expansion, aur `KEY=v # comment`.

### `express::logger` (morgan)

```cpp
app.use(express::logger("dev"));        // GET /users 200 0.604 ms - 65  (rangin status)
app.use(express::logger("combined"));   // tiny | short | common | combined
app.use(express::logger(":method :url :status :response-time ms"));
app.use(express::logger("tiny", {.sink = [](const std::string &l) { /* file me likho */ },
                                 .skip = [](const express::Req &, const express::FinishInfo &i) { return i.status < 400; }}));
```

### `express::cors`

```cpp
// Tareeka 1 (Node style)
app.use(express::cors({.origins = {express::process.env["CLIENT_URL"] || "*"}, .credentials = true, .maxAge = 600}));
// Tareeka 2 (Express++ style), dono same hain
app.use(express::cors({.origins = {express::env::get("CLIENT_URL", "*")}, .credentials = true, .maxAge = 600}));
```

Preflight (OPTIONS) request ka jawab Express++ khud 204 se de deta hai.

### `express::nv` (nevla glue)

```cpp
#include <express/nevla.hpp>
express::nv::use(app, express::process.env["MONGODB_URI"] || "memory://dev", "mydb");      // Tareeka 1
// express::nv::use(app, express::env::get("MONGODB_URI", "memory://dev"), "mydb");      // Tareeka 2 (same)
```

Ye teen kaam karta hai:
- `nevla::useDrogonLoop()` lagata hai, taaki har `co_await` ke baad handler apne IO loop pe hi wapas aaye.
- `nevla::connect()` karta hai.
- Nevla ke errors ko HTTP status me badalta hai: `ValidationError`/`CastError` → 400 (`errors: {field: message}` ke saath), `NotFoundError` → 404, `DuplicateKeyError`/`VersionError` → 409.

Isme do helpers bhi hain: `express::nv::body(req)` (galat JSON pe 400 deta hai) aur `express::nv::query(req)` (`Model::fromQuery` ke liye).

## Tests

`ctest` ek asli server chalata hai aur us pe asli HTTP requests bhejta hai. Isme **196 checks** hain, jo in sab ko cover karte hain:
- **Routing:** `route()`, optional/regex params, regex paths, `app.param()`
- **Middleware aur errors:** router error middleware (chain, async, recover, rethrow), `next()` guard
- **Response:** download/attachment, render (partials, escaping, loops, locals, custom engine), format (406, default), append/vary/location
- **Request:** `req.accepts/is/hostname/protocol/xhr`, trust proxy
- **Packages:** serveStatic (ETag/304, index, extensions, dotfiles, SPA fallback, `..` protection), `json()`/`bodyLimit` (400/413), CORS, dotenv
- **Security:** jwt (jwt.io token, HS384/512, RS256, expired/nbf/audience/alg-none), auth middleware (Bearer, cookie, optional), argon2id + bcrypt (`$2a$` compat), validate (errors, coercion, defaults, strict, custom), helmet, rateLimit (429, headers, reset, skipSuccessfulRequests), upload (disk, memory, limits, filter, unexpected field), cookieParser (Node ke signed cookies padhna, rotation, tamper), crypto (Node / RFC vectors, AES-GCM tamper, lazy `final()`)
- **Nevla:** CRUD + validation + 300 concurrent coroutine requests, jisme check hota hai ki har `co_await` apne IO loop pe resume hua

ASan + UBSan ke saath bhi saare tests clean pass hote hain.

## Performance (Express++ vs pure Drogon)

Ek hi API dono tarah likhi gayi (1 IO thread, server aur `wrk` alag CPU core pe, 100 connections, Release build). Numbers requests/sec hain.

| Route | Pure Drogon | Express++ | Express++ / Drogon |
|-------|------------:|----------:|------:|
| `GET /plaintext` | 164k | 95k | 58% |
| `GET /json` | 71k | 66k | 94% |
| `GET /api/users/:id` (param + query) | 53k | 46k | 86% |
| `POST /api/echo` (JSON body) | 38k | 35k | 93% |
| `GET /api/secure` (JWT verify) | 36k | 32k | 88% |
| `GET /api/db/:id` (Nevla, co_await) | 7.3k | 7.4k | ~100% |

Helmet + CORS + `json()` + 50 extra routes ke saath ("asli app" jaisa): Drogon (same headers, post-handling advice se) plaintext 78k / json 53k / echo 32k / db 6.8k; Express++ 36k / 34k / 24k / 6.4k.

**Matlab:** khaali route pe Drogon kaafi aage hai (Express++ ka router + middleware chain + per-request objects ka kharcha dikhta hai). Jaise hi route asli kaam karta hai (JSON, JWT, database), fark chhota ho jaata hai, aur database wale route pe dono barabar hain. Ek-do bahut garam endpoints ko pure Drogon me likh sakte ho, baaki Express++ me.

## Examples

- `examples/01_hello.cc`: Express ka hello world
- `examples/02_users_api.cc`: Express++ + nevla REST API (`memory://` pe chalta hai, MongoDB ki zarurat nahi)
- `examples/03_website.cc`: static files + templates + `res.format` + `app.param` + router error middleware
- `examples/04_auth_api.cc`: signup/login/profile/avatar: validate + hash + jwt + auth + helmet + rateLimit + upload
- Poora project structure (routes / controllers / models / services / jobs, sab packages ke saath): **expresspp-starter**

## Roadmap

`express::compression`, `express::session` + signed cookies, `express::csrf`, Redis store (rateLimit/session), `express::socket` (socket.io jaisa), `express::health` + graceful shutdown, `expresspp` CLI (`create`, `dev`, `test`, `db:sync`).

## Abhi ki limits

- Route matching case-sensitive hai, aur trailing slash ignore hota hai. Express ke `caseSensitive` aur `strict` options abhi nahi hain.
- Regex groups ke naam nahi hote (`std::regex` named groups support nahi karta). Unhe `req.param("0")`, `req.param("1")` se lo.
- `res.jsonp()`, `res.links()`, `req.subdomains`, `req.fresh` abhi nahi hain.
- Drogon 1.8.7 (Ubuntu package) pe test kiya gaya hai.
