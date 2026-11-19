#include "express/helmet.hpp"

namespace express {

Middleware helmet(HelmetOptions o) {
    std::vector<std::pair<std::string, std::string>> h;
    if (!o.contentSecurityPolicy.empty())
        h.emplace_back(o.cspReportOnly ? "Content-Security-Policy-Report-Only" : "Content-Security-Policy",
                       o.contentSecurityPolicy);
    if (!o.crossOriginEmbedderPolicy.empty()) h.emplace_back("Cross-Origin-Embedder-Policy", o.crossOriginEmbedderPolicy);
    if (!o.crossOriginOpenerPolicy.empty()) h.emplace_back("Cross-Origin-Opener-Policy", o.crossOriginOpenerPolicy);
    if (!o.crossOriginResourcePolicy.empty()) h.emplace_back("Cross-Origin-Resource-Policy", o.crossOriginResourcePolicy);
    if (o.originAgentCluster) h.emplace_back("Origin-Agent-Cluster", "?1");
    if (!o.referrerPolicy.empty()) h.emplace_back("Referrer-Policy", o.referrerPolicy);
    if (o.hsts) {
        std::string v = "max-age=" + std::to_string(o.hstsMaxAge);
        if (o.hstsIncludeSubDomains) v += "; includeSubDomains";
        if (o.hstsPreload) v += "; preload";
        h.emplace_back("Strict-Transport-Security", v);
    }
    if (o.noSniff) h.emplace_back("X-Content-Type-Options", "nosniff");
    h.emplace_back("X-DNS-Prefetch-Control", o.dnsPrefetchControl ? "on" : "off");
    if (o.ieNoOpen) h.emplace_back("X-Download-Options", "noopen");
    if (!o.frameguard.empty()) h.emplace_back("X-Frame-Options", o.frameguard);
    if (!o.permittedCrossDomainPolicies.empty())
        h.emplace_back("X-Permitted-Cross-Domain-Policies", o.permittedCrossDomainPolicies);
    if (o.xssFilter) h.emplace_back("X-XSS-Protection", "0");
    return [h](Req, Res res, Next next) {
        auto &hs = res.state().headers;
        if (hs.empty())  // usual case (helmet runs first): no duplicates possible, skip the per-header search
            hs.insert(hs.end(), h.begin(), h.end());
        else
            for (const auto &[k, v] : h) res.set(k, v);
        next();
    };
}

}  // namespace express
