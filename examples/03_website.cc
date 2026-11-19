/* A small website + API: static files, templates, content negotiation, router error middleware.
     ./03_website      (run it from the folder that has public/ and views/, see below)

   It creates public/ and views/ next to itself on first start so it works out of the box.
     curl localhost:3000/                   -> public/index.html
     curl localhost:3000/hello/Asha         -> rendered views/hello.html
     curl -H 'Accept: application/json' localhost:3000/hello/Asha   -> JSON instead
     curl localhost:3000/api/books/12       -> {"id":"12"}      /api/books/abc -> 400 from the error middleware */
#include <express/express.hpp>

#include <filesystem>
#include <fstream>

static void ensure(const std::string &path, const std::string &text) {
    if (std::filesystem::exists(path)) return;
    std::filesystem::create_directories(std::filesystem::path(path).parent_path());
    std::ofstream(path) << text;
}

int main() {
    ensure("public/index.html", "<h1>Express++ website</h1><a href=\"/hello/World\">say hello</a>\n");
    ensure("views/layout_header.html", "<header>{{site}}</header>\n");
    ensure("views/hello.html", "{{> layout_header}}<p>Hello {{name}}!</p>{{#tags}}<i>{{.}}</i>{{/tags}}\n");

    express::env::load();
    express::App app;
    app.set("views", "views");
    app.set("trust proxy", express::env::getBool("BEHIND_PROXY", false));
    app.locals()["site"] = "My Express++ Site";

    app.use(express::logger("dev"));
    app.use(express::json());
    app.use(express::serveStatic("public", {.maxAge = 3600, .extensions = {".html"}}));

    app.get("/hello/:name", [](express::Req req, express::Res res) {
        auto data = express::obj("name", req.param("name"), "tags", express::arr("c++", "fast"));
        res.format({{"html", [res, data]() mutable { res.render("hello", data); }},
                    {"json", [res, data]() mutable { res.json(data); }}});
    });

    express::Router api;
    api.param("id", [](express::Req, express::Res, express::Next next, std::string id) {
        if (id.find_first_not_of("0123456789") != std::string::npos) throw express::HttpError(400, "id must be a number");
        next();
    });
    api.route("/books/:id")
        .get([](express::Req req, express::Res res) { res.json(express::obj("id", req.param("id"))); })
        .del([](express::Req, express::Res res) { res.sendStatus(204); });
    api.use([](express::Error err, express::Req req, express::Res res, express::Next) {
        res.status(err.status()).json(express::obj("error", err.message(), "where", req.originalUrl()));
    });
    app.use("/api", api);

    int port = static_cast<int>(express::env::getInt("PORT", 3000));
    app.listen(port, "0.0.0.0", [port] { std::printf("website on http://localhost:%d\n", port); });
}
