#pragma once
// App-wide state shared by App, Req and Res (internal; use the App / Req / Res methods instead).
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <unordered_map>

#include "json.hpp"

namespace express::detail {

struct AppContext {
    bool trustProxy = false;
    std::string views = "views";  // folder for res.render()
    std::string viewExt = ".html";
    int viewCache = -1;      // -1 = on in production, 0 = off, 1 = on
    size_t bodyLimit = 0;    // 0 = Drogon's default (1 MB)
    Json locals = Json(::Json::objectValue);  // app.locals()
    std::map<std::string, std::string> settings;  // app.set("x", "y") for anything else
    // ext (".mst") -> engine(filePath, data) -> html
    std::map<std::string, std::function<std::string(const std::string &, const Json &)>> engines;

    std::mutex cacheMutex;
    std::unordered_map<std::string, std::string> templateCache;
};

}  // namespace express::detail
