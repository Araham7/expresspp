/* Signup / login / profile / avatar upload: every v0.3 security package in one small API.
   Users are kept in memory to keep the example short (use nevla in a real app).

     curl -X POST localhost:3000/signup -H 'content-type: application/json' \
          -d '{"email":"asha@example.com","password":"longpassword"}'
     curl -X POST localhost:3000/login  -H 'content-type: application/json' \
          -d '{"email":"asha@example.com","password":"longpassword"}'            -> {"token": "..."}
     curl localhost:3000/me -H "Authorization: Bearer <token>"
     curl -X POST localhost:3000/me/avatar -H "Authorization: Bearer <token>" -F avatar=@photo.png   */
#include <express/express.hpp>

#include <map>
#include <mutex>

namespace v = express::v;

static std::mutex gMutex;
static std::map<std::string, std::string> gUsers;  // email -> password hash

int main() {
    express::env::load();
    std::string secret = express::process.env["JWT_SECRET"] || "dev-only-secret-change-me";

    express::App app;
    app.disable("x-powered-by");
    app.set("trust proxy", express::env::getBool("BEHIND_PROXY", false));
    app.set("body limit", "5mb");

    app.use(express::helmet());
    app.use(express::logger("dev"));
    app.use(express::cors({.origins = {express::process.env["CLIENT_URL"] || "*"}}));
    app.use(express::json());
    app.use(express::rateLimit({.windowMs = 15 * 60 * 1000, .limit = 300}));

    auto credentials = v::object({
        {"email", v::string().trim().lowercase().email().required()},
        {"password", v::string().min(8).max(72).required()},
    });

    app.post("/signup", express::validate({.body = credentials}), [](express::Req req, express::Res res) -> express::Task<> {
        const auto &body = express::validated(req);
        std::string email = body["email"].asString();
        {
            std::lock_guard<std::mutex> lk(gMutex);
            if (gUsers.count(email)) throw express::HttpError(409, "Email already registered");
        }
        std::string hash = co_await express::hash::makeAsync(body["password"].asString());
        {
            std::lock_guard<std::mutex> lk(gMutex);
            gUsers[email] = hash;
        }
        res.status(201).json(express::obj("email", email));
    });

    app.post("/login",
             express::rateLimit({.windowMs = 60 * 1000, .limit = 5, .message = "Too many login attempts",
                                 .skipSuccessfulRequests = true}),
             express::validate({.body = credentials}),
             [secret](express::Req req, express::Res res) -> express::Task<> {
                 const auto &body = express::validated(req);
                 std::string email = body["email"].asString(), hash;
                 {
                     std::lock_guard<std::mutex> lk(gMutex);
                     if (auto it = gUsers.find(email); it != gUsers.end()) hash = it->second;
                 }
                 bool ok = !hash.empty() && co_await express::hash::verifyAsync(body["password"].asString(), hash);
                 if (!ok) throw express::HttpError(401, "Wrong email or password");
                 auto token = express::jwt::sign(express::obj("email", email), secret, {.expiresIn = "7d"});
                 res.json(express::obj("token", token));
             });

    express::Router me;
    me.use(express::auth({.secret = secret}));
    me.get("/", [](express::Req req, express::Res res) { res.json(express::jwt::user(req)); });
    auto upload = express::upload({.dest = "avatars",
                                   .limits = {.fileSize = "2mb", .files = 1},
                                   .fileFilter = [](const express::UploadedFile &f) { return f.mimetype.rfind("image/", 0) == 0; }});
    me.post("/avatar", upload.single("avatar"), [](express::Req req, express::Res res) {
        auto *f = express::file(req, "avatar");
        if (!f) throw express::HttpError(400, "Send an image in the 'avatar' field");
        res.status(201).json(express::obj("saved", f->path, "bytes", f->size));
    });
    app.use("/me", me);

    int port = static_cast<int>(express::process.env["PORT"] || 3000);
    app.listen(port, "0.0.0.0", [port] { std::printf("auth API on http://localhost:%d\n", port); });
}
