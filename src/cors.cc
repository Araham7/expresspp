#include "express/cors.hpp"

#include <algorithm>

namespace express {

Middleware cors(CorsOptions o) {
    bool any = std::find(o.origins.begin(), o.origins.end(), "*") != o.origins.end();
    return [o, any](Req req, Res res, Next next) {
        const std::string &origin = req.header("origin");
        std::string allow;
        if (any) {
            allow = (o.credentials && !origin.empty()) ? origin : "*";
            if (allow != "*") res.set("Vary", "Origin");
        } else {
            res.set("Vary", "Origin");
            if (!origin.empty() && std::find(o.origins.begin(), o.origins.end(), origin) != o.origins.end())
                allow = origin;
        }
        if (!allow.empty()) {
            res.set("Access-Control-Allow-Origin", allow);
            if (o.credentials) res.set("Access-Control-Allow-Credentials", "true");
            if (!o.exposedHeaders.empty()) res.set("Access-Control-Expose-Headers", o.exposedHeaders);
        }

        bool preflight = req.method() == "OPTIONS" && !req.header("access-control-request-method").empty();
        if (!preflight) {
            next();
            return;
        }
        if (!allow.empty()) {
            res.set("Access-Control-Allow-Methods", o.methods);
            std::string hdrs = o.allowedHeaders.empty() ? req.header("access-control-request-headers") : o.allowedHeaders;
            if (!hdrs.empty()) res.set("Access-Control-Allow-Headers", hdrs);
            if (o.maxAge >= 0) res.set("Access-Control-Max-Age", std::to_string(o.maxAge));
        }
        res.sendStatus(o.preflightStatus);
    };
}

}  // namespace express
