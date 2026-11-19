#pragma once
/* express::logger: morgan for Express++.

     app.use(express::logger("dev"));        // GET /users/42 200 1.234 ms - 57   (status in colour)
     app.use(express::logger("tiny"));       // GET /users/42 200 57 - 1.234 ms
     app.use(express::logger("combined"));   // Apache combined log line
     app.use(express::logger("short"));
     app.use(express::logger(":method :url -> :status in :response-time ms"));   // your own format

   Tokens: :method :url :status :response-time :res[content-length] :remote-addr :date
           :user-agent :referrer :http-version :req[<header>]
   Lines go to stdout by default; pass a sink to send them somewhere else:
     app.use(express::logger("tiny", [](const std::string &line) { myLog.info(line); }));
   The line is written when the response is sent, so the status and time are the real ones.      */

#include <functional>
#include <string>

#include "app.hpp"

namespace express {

struct LoggerOptions {
    std::function<void(const std::string &)> sink;  // default: stdout
    std::function<bool(const Req &, const FinishInfo &)> skip;  // e.g. skip 2xx: return i.status < 400
};

Middleware logger(const std::string &format = "dev", LoggerOptions opts = {});
inline Middleware logger(const std::string &format, std::function<void(const std::string &)> sink) {
    LoggerOptions o;
    o.sink = std::move(sink);
    return logger(format, std::move(o));
}

}  // namespace express
