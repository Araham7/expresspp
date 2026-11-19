#pragma once
/* Express++ + nevla (header-only; link nevla::nevla too).

     #include <express/express.hpp>
     #include <express/nevla.hpp>

     express::App app;
     express::nv::use(app, express::env::get("MONGODB_URI", "memory://dev"), "iot");

     app.post("/users", [](express::Req req, express::Res res) -> express::Task<> {
         auto doc = co_await User().createAsync(express::nv::body(req));
         res.status(201).json(doc.toJSON());          // nevla::Value goes straight into res.json
     });

   express::nv::use() does three things:
     1. nevla::useDrogonLoop()  ->  after every co_await the handler is back on its own IO loop
     2. nevla::connect(uri, db)
     3. error mapping:
          nevla::ValidationError   -> 400 {"error": "...", "errors": {field: message}}
          nevla::CastError         -> 400 {"error": "..."}
          nevla::NotFoundError     -> 404 {"error": "..."}
          nevla::DuplicateKeyError -> 409 {"error": "..."}
          nevla::VersionError      -> 409 {"error": "..."}                                          */

#include <nevla/drogon.hpp>
#include <nevla/nevla.hpp>

#include "app.hpp"

namespace express::nv {

// request body -> nevla::Value ({} when the body is empty)
inline ::nevla::Value body(const Req &req) {
    std::string b = req.body();
    if (b.find_first_not_of(" \t\r\n") == std::string::npos) return ::nevla::Value::object();
    try {
        return ::nevla::Value::parse(b);
    } catch (const std::exception &e) {
        throw HttpError(400, std::string("Invalid JSON body: ") + e.what());
    }
}

// query string -> nevla::Value of strings (handy with Model::fromQuery)
inline ::nevla::Value query(const Req &req) {
    ::nevla::Value o = ::nevla::Value::object();
    for (const auto &[k, v] : req.queries()) o[k] = v;
    return o;
}

inline void errors(App &app) {
    app.onError<::nevla::CastError>([](const ::nevla::CastError &e, Req, Res res) {
        res.status(400).json(express::obj("error", e.what()));
    });
    app.onError<::nevla::NotFoundError>([](const ::nevla::NotFoundError &e, Req, Res res) {
        res.status(404).json(express::obj("error", e.what()));
    });
    app.onError<::nevla::DuplicateKeyError>([](const ::nevla::DuplicateKeyError &e, Req, Res res) {
        res.status(409).json(express::obj("error", e.what()));
    });
    app.onError<::nevla::VersionError>([](const ::nevla::VersionError &e, Req, Res res) {
        res.status(409).json(express::obj("error", e.what()));
    });
    app.onError<::nevla::ValidationError>([](const ::nevla::ValidationError &e, Req, Res res) {
        res.status(400).json(express::obj("error", e.what(), "errors", e.toJSON()));
    });
}

inline void use(App &app, const std::string &uri, const std::string &db = "") {
    ::nevla::useDrogonLoop();
    ::nevla::connect(uri, db);
    errors(app);
}

}  // namespace express::nv
