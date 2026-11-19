// The Express "hello world", in Express++.   ./01_hello   then   curl localhost:3000/users/42
#include <express/express.hpp>

int main() {
    express::env::load();  // .env if there is one
    express::App app;
    app.use(express::logger("dev"));
    app.use(express::cors({.origins = {express::env::get("CLIENT_URL", "*")}}));

    app.get("/", [](express::Req, express::Res res) { res.send("Hello from Express++ 👋"); });

    app.get("/users/:id", [](express::Req req, express::Res res) {
        res.json(express::obj("id", req.param("id"), "page", req.query("page", "1")));
    });

    app.post("/echo", [](express::Req req, express::Res res) { res.json(req.json()); });

    express::Router api;
    api.get("/health", [](express::Req, express::Res res) { res.json(express::obj("ok", true)); });
    app.use("/api", api);

    int port = static_cast<int>(express::env::getInt("PORT", 3000));
    app.listen(port, "0.0.0.0", [port] { std::printf("Express++ listening on http://localhost:%d\n", port); });
}
