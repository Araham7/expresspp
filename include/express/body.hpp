#pragma once
/* Body helpers: express.json() and body size limits.

     app.set("body limit", "10mb");          // biggest body Drogon accepts at all (default 1mb)
     app.use(express::json());               // bad JSON -> 400, too big -> 413 (like express.json())
     app.use(express::json({.limit = "100kb", .strict = true}));
     app.post("/upload", express::bodyLimit("5mb"), handler);   // per route

   Drogon already parses JSON and urlencoded bodies: req.json() and req.query("field") just work.
   express::json() adds the checks Express users expect.                                         */

#include <string>

#include "app.hpp"

namespace express {

// "100kb" / "1mb" / "2gb" / "512" (bytes) -> bytes; throws std::invalid_argument on junk
size_t parseBytes(const std::string &text);

struct JsonOptions {
    std::string limit = "1mb";
    bool strict = true;               // only objects and arrays at the top level
    std::string type = "json";        // which Content-Type to check ("json" also matches +json)
};
Middleware json(JsonOptions o = {});

Middleware bodyLimit(const std::string &limit);

}  // namespace express
