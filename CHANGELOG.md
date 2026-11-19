# Changelog

## [0.4.2] - 2026-11-19

- **Fix (CMake):** `expressppConfig.cmake` ab Drogon ko dobara load nahi karta agar wo pehle se loaded hai. Pehle ek project me kai packages saath dhundhne par (khaas kar `find_package(... CONFIG)` bina `REQUIRED` ke) "UUID_lib already exists" jaisa CMake error aa sakta tha. Code me koi badlaav nahi.

## [0.4.1] - 2026-11-19

Speed (pure Drogon ke saath benchmark ke baad):
- `res.json()` / `express::stringify`: JSON writer har thread pe ek baar banta hai (pehle har request pe naya `StreamWriterBuilder`). JSON route **~45%** tez.
- Router: path ke segments har request pe ek hi baar split hote hain, har layer pe nahi. Method check bina `std::string` banaye.
- `res.locals()` ka empty object ab sirf use hone par banta hai (har request pe ek allocation kam)
- `helmet()`: headers seedha jud jaate hain jab pehle se koi header set nahi (aam case)
- `express::parseJson`: reader har thread pe ek baar
- API me koi badlaav nahi, 196 tests pass

## [0.4.0] - 2026-11-19

- **`express::cookieParser(secret)`** (cookie-parser): `req.cookies()`, `req.signedCookie(name)`, `req.signedCookies()`, `req.secret()`, `res.cookie(..., {.sign = true})`, secret rotation (`{"new", "old"}`). Signature format Node ke `cookie-parser` / `cookie-signature` jaisa (asli Node package se check kiya).
- **`express::crypto`** (Node `crypto`): `Buffer`, `randomBytes`, `randomUUID`, `randomInt`, `createHash`, `createHmac`, `timingSafeEqual`, `pbkdf2Sync`, `scryptSync`, `createCipheriv` / `createDecipheriv` (GCM, CBC, CTR, ChaCha20-Poly1305, `setAAD`, `getAuthTag`, `setAuthTag`). Output Node se match.
- `res.cookie()` value ko `encodeURIComponent` karta hai, `req.cookie()` decode karta hai (Express jaisa). `CookieOptions` me `.sign` aur `.domain` naye.
- `update(...) + final(...)` ek line me safe (C++ ka operand order koi farak nahi daalta)
- 196 tests (29 naye), ASan/UBSan clean

## [0.3.4] - 2026-11-19

- Docs: saathi packages ke naye naam (axiospp, nodemailerpp, socketiopp, bullmqpp). Code me koi badlaav nahi.

## [0.3.3] - 2026-11-19

- Install hone ke baad CMake target naam ab README jaise hain: `expresspp::core`, `expresspp::env`, `expresspp::logger`, `expresspp::cors`, `expresspp::security`, `expresspp::nevla`. Pehle ye `expresspp::expresspp_core` jaise export hote the, aur `expresspp::nevla` install hi nahi hota tha.
- `expresspp::nevla` tabhi load hota hai jab Nevla bhi install ho
- Check kiya: ek hi app me Express++ + Nevla + axiospp + nodemailerpp + socketiopp + bullmqpp saath build hote aur chalte hain

## [0.3.2] - 2026-11-19

- WebSocket upgrade requests ab seedha Drogon ke WebSocket controllers ke paas jaati hain (socketiopp, ya tumhari apni `WS_PATH_ADD` classes). Pehle Express++ unhe 404 de deta tha. Node me bhi Express "upgrade" requests ko nahi chhoota.
- Test: Express++ app ke saath ek Drogon WebSocket controller pe asli WS client se baat (167 checks)

## [0.3.1] - 2026-11-19

- README: environment variables ke dono tareeke (`express::process.env` aur `express::env`) har jagah saath-saath dikhaye gaye hain, ek table me
- `express::env::get(key, def)` ab khaali value (`KEY=`) pe bhi default deta hai, bilkul `process.env["KEY"] || def` ki tarah. Pehle ye `""` deta tha.
- Test jo pakka karta hai ki dono tareeke same result dete hain (166 checks)

## [0.3.0] - 2026-11-19

Roz kaam aane wale security packages, ab Express++ ke andar (`expresspp::security`).

- **`express::jwt`**: sign/verify/decode (HS256/384/512, RS256), `expiresIn`/`notBefore`/`iss`/`aud`/`sub`/`maxAge`, jsonwebtoken jaise errors (401). jwt.io aur Node ke tokens ke saath compatible.
- **`express::auth()`** (express-jwt): Bearer header ya cookie, `credentialsRequired`, `isRevoked`, `express::jwt::user(req)`
- **`express::hash`**: argon2id (libsodium, Node `argon2` jaisa format) aur bcrypt (`$2a$/$2b$/$2y$`, Node `bcrypt` ke hash verify hote hain), `needsRehash`, aur `makeAsync`/`verifyAsync` jo worker pool pe chalte hain
- **`express::validate`** + `express::v`: joi/zod jaise schemas, field errors ke saath 400, query/params coercion, defaults, unknown keys hatana/strict/passthrough, `custom()`, `express::validated(req)`
- **`express::helmet`**: helmet 7 ke default headers, har header ka option
- **`express::rateLimit`**: express-rate-limit (fixed window MemoryStore, standard/legacy headers, Retry-After, skip/skipSuccessfulRequests/skipFailedRequests, `RateLimitStore` interface)
- **`express::upload`**: multer (`single`/`array`/`fields`/`any`/`none`, disk/memory storage, limits, fileFilter, multer jaise errors)
- **`express::offload(fn)`**: blocking kaam worker pool pe, `co_await` ke saath, aur kaam ke baad wapas usi IO loop pe
- **`express::process.env`**: Node jaisa `process.env["X"] || default`
- **`HttpError`** me extra JSON fields (`HttpError(400, "msg", obj(...))`) aur `.header(k, v)`
- `app.disable("x-powered-by")` se Drogon ka `Server` header band hota hai
- Node ke `jsonwebtoken`, `argon2` aur `bcryptjs` packages ke saath dono taraf check kiya: Node ke tokens/hashes yahan verify hote hain, aur yahan ke Node me
- Naya example `04_auth_api`; tests 95 se badh ke 164; ASan/UBSan clean

## [0.2.0] - 2026-11-19

Express ki baaki cheezein.

### Routing
- `app.route(path)` chaining: `.get().post().put().patch().del().all()`
- Optional params `:id?`, regex constraint `:id(\\d+)`, poore regex paths `std::regex(...)` (groups → `req.param("0")`, ...)
- `app.param(name, fn)` / `router.param(...)`: har request me ek baar chalta hai, coroutine bhi ho sakta hai
- `app.use("/path", middleware)`: middleware ke andar `req.path()` mount path ke baad wala hissa hota hai, aur `req.baseUrl()` mount path (Express jaisa)

### Error handling
- Router-level error middleware: `void(express::Error, Req, Res, Next)` (aur `Task<>` version)
- Error aane pe Express jaisa flow: normal layers skip hoti hain, error layers chalti hain. `next()` se recover hota hai, `next(err)` se error aage jaata hai, aur error parent router tak bubble hota hai.
- `express::Error`: `message()`, `status()`, `is<T>()`, `as<T>()`, `rethrow()`, `ptr()`
- `next()` ek hi baar chalta hai, doosri baar ignore aur log hota hai

### Response
- `res.download()`, `res.attachment()`, `res.render()` / `renderToString()`, `res.format()`, `res.append()`, `res.get()`, `res.location()`, `res.vary()`, `res.locals()`
- `res.type("json" | "html" | "text" | "csv" | ...)` shorthand
- Built-in Mustache template engine (`express::renderTemplate`), `app.engine()`, `app.locals()`, `view cache`

### Request
- `req.accepts()`, `req.is()`, `req.hostname()`, `req.protocol()`, `req.secure()`, `req.xhr()`, `req.ips()`
- `app.set("trust proxy", true)`: `X-Forwarded-For/-Proto/-Host`

### Packages (core me)
- `express::serveStatic` (express.static): index, maxAge/immutable, dotfiles, extensions, SPA fallback, ETag/Last-Modified/304, path traversal se bachav
- `express::json()` (express.json): 400 aur 413 errors, strict mode, `+json` types
- `express::bodyLimit()` aur `app.set("body limit", "10mb")` (Drogon ki global limit bhi isi se set hoti hai)

### Baaki
- `app.set/setting/enable/disable/enabled`
- Production env variable ka naam ab `EXPRESS_ENV` hai (`NODE_ENV` bhi chalta hai)
- Naya example: `03_website`
- Tests 48 se badh ke 95 checks; ASan/UBSan clean

## [0.1.0] - 2026-11-19

Pehli release (pehle ka naam "dex" tha).

- `App`, `Router`, `Req`, `Res`
- Middleware chain, `co_await` handlers, `HttpError`, `onError<T>()`
- `express::env` (dotenv), `express::logger` (morgan), `express::cors`
- nevla glue `express::nv`
