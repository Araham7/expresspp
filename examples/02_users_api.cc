/* A small REST API: Express++ + nevla, coroutine handlers, validation errors as 400.
     ./02_users_api                      (memory://, no MongoDB needed)
     MONGODB_URI=mongodb://127.0.0.1:27017 ./02_users_api

     curl -X POST localhost:3000/api/users -H 'content-type: application/json' -d '{"name":"Asha","age":21}'
     curl -X POST localhost:3000/api/users -H 'content-type: application/json' -d '{"age":-3}'   -> 400
     curl 'localhost:3000/api/users?minAge=18'
     curl localhost:3000/api/users/<id>                                                          */
#include <express/express.hpp>
#include <express/nevla.hpp>

static nevla::Model User() { return nevla::model("User"); }

int main() {
    express::env::load();
    express::App app;
    app.use(express::logger("dev"));
    app.use(express::cors());

    express::nv::use(app, express::env::get("MONGODB_URI", "memory://dex_demo"), express::env::get("MONGODB_DB", "dex_demo"));
    nevla::model("User", nevla::Schema({{"name", nevla::String().required()},
                                        {"age", nevla::Int().min(0)}}));

    express::Router users;

    users.get("/", [](express::Req req, express::Res res) -> express::Task<> {
        int minAge = std::atoi(req.query("minAge", "0").c_str());
        auto rows = co_await User().find(nevla::obj("age", nevla::obj("$gte", minAge))).limit(50).leanAsync();
        nevla::Value list = nevla::Value::array();
        for (auto &r : rows) list.push_back(r);
        res.json(nevla::obj("count", static_cast<int64_t>(rows.size()), "users", list));
    });

    users.get("/:id", [](express::Req req, express::Res res) -> express::Task<> {
        auto doc = co_await User().findById(req.param("id")).leanAsync();
        if (!doc) throw express::HttpError(404, "User not found");
        res.json(*doc);
    });

    users.post("/", [](express::Req req, express::Res res) -> express::Task<> {
        auto doc = co_await User().createAsync(express::nv::body(req));  // ValidationError -> 400 automatically
        res.status(201).json(doc.toJSON());
    });

    app.use("/api/users", users);

    int port = static_cast<int>(express::env::getInt("PORT", 3000));
    app.listen(port, "0.0.0.0", [port] { std::printf("users API on http://localhost:%d/api/users\n", port); });
}
