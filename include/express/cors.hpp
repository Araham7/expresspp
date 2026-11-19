#pragma once
/* express::cors: the cors package for Express++.

     app.use(express::cors());                                         // Access-Control-Allow-Origin: *
     app.use(express::cors({.origins = {"http://localhost:5173"}, .credentials = true}));
     app.use(express::cors({.origins = {express::env::get("CLIENT_URL")}}));

   Preflight (OPTIONS with Access-Control-Request-Method) is answered with 204 right here.
   With a list of origins, only those get the header (and Vary: Origin is set).
   credentials=true never sends "*": the request's Origin is echoed back instead.                 */

#include <string>
#include <vector>

#include "app.hpp"

namespace express {

struct CorsOptions {
    std::vector<std::string> origins{"*"};
    std::string methods = "GET,HEAD,PUT,PATCH,POST,DELETE";
    std::string allowedHeaders;  // empty: echo Access-Control-Request-Headers
    std::string exposedHeaders;
    bool credentials = false;
    int maxAge = -1;  // seconds the browser may cache the preflight
    int preflightStatus = 204;
};

Middleware cors(CorsOptions o = {});

}  // namespace express
