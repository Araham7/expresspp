#pragma once
/* express::serveStatic: express.static() for Express++ (static is a C++ keyword, so the name differs).

     app.use(express::serveStatic("public"));                         // GET /logo.png -> public/logo.png
     app.use("/assets", express::serveStatic("dist/assets", {.maxAge = 86400}));
     app.use(express::serveStatic("dist", {.spaFallback = "index.html"}));   // React/Vite apps

   - only GET / HEAD; anything else goes to next()
   - folder -> its index.html; "/about" can find about.html with .extensions = {".html"}
   - "..", absolute paths and NUL bytes can never escape the root folder
   - dotfiles (.env, .git/...) are hidden by default
   - Last-Modified + ETag, answers If-None-Match / If-Modified-Since with 304
   - not found: next() (fallthrough = true, like Express) or 404                                    */

#include <string>
#include <vector>

#include "app.hpp"

namespace express {

struct StaticOptions {
    std::string index = "index.html";  // "" = no index files
    int maxAge = 0;                     // Cache-Control max-age in seconds
    bool immutable = false;             // adds ", immutable" (for hashed file names)
    std::string dotfiles = "ignore";    // "ignore" (pretend missing) | "deny" (403) | "allow"
    bool fallthrough = true;            // missing file: next() instead of 404
    std::vector<std::string> extensions;  // try these when the exact file is missing, e.g. {".html"}
    std::string spaFallback;            // serve this file (relative to root) for unknown paths that accept HTML
    bool etag = true;
    bool lastModified = true;
};

Middleware serveStatic(const std::string &root, StaticOptions o = {});
inline Middleware static_(const std::string &root, StaticOptions o = {}) { return serveStatic(root, std::move(o)); }

}  // namespace express
