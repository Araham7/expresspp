#pragma once
/* express::helmet: the helmet package for Express++ (same default headers as helmet 7/8).

     app.use(express::helmet());
     app.use(express::helmet({.contentSecurityPolicy = "default-src 'self'; img-src * data:"}));
     app.use(express::helmet({.contentSecurityPolicy = "", .hsts = false}));   // "" / false = leave that header out

   Default headers:
     Content-Security-Policy        default-src 'self';base-uri 'self';font-src 'self' https: data:;...
     Cross-Origin-Opener-Policy     same-origin
     Cross-Origin-Resource-Policy   same-origin
     Origin-Agent-Cluster           ?1
     Referrer-Policy                no-referrer
     Strict-Transport-Security      max-age=31536000; includeSubDomains
     X-Content-Type-Options         nosniff
     X-DNS-Prefetch-Control         off
     X-Download-Options             noopen
     X-Frame-Options                SAMEORIGIN
     X-Permitted-Cross-Domain-Policies  none
     X-XSS-Protection               0
   helmet also hides X-Powered-By; Drogon's version is the "Server: drogon/x.y" header:
   app.disable("x-powered-by") turns that off for the whole server.                                 */

#include <string>

#include "app.hpp"

namespace express {

struct HelmetOptions {
    std::string contentSecurityPolicy =
        "default-src 'self';base-uri 'self';font-src 'self' https: data:;form-action 'self';"
        "frame-ancestors 'self';img-src 'self' data:;object-src 'none';script-src 'self';"
        "script-src-attr 'none';style-src 'self' https: 'unsafe-inline';upgrade-insecure-requests";
    bool cspReportOnly = false;
    std::string crossOriginEmbedderPolicy;  // "" = off (helmet's default); "require-corp"
    std::string crossOriginOpenerPolicy = "same-origin";
    std::string crossOriginResourcePolicy = "same-origin";
    bool originAgentCluster = true;
    std::string referrerPolicy = "no-referrer";
    bool hsts = true;
    int hstsMaxAge = 31536000;
    bool hstsIncludeSubDomains = true;
    bool hstsPreload = false;
    bool noSniff = true;
    bool dnsPrefetchControl = false;  // false -> "off"
    bool ieNoOpen = true;
    std::string frameguard = "SAMEORIGIN";  // "DENY" | "" (off)
    std::string permittedCrossDomainPolicies = "none";
    bool xssFilter = true;  // sends "X-XSS-Protection: 0" (turns the broken old filter off)
};

Middleware helmet(HelmetOptions o = {});

}  // namespace express
