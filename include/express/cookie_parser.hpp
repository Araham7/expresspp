#pragma once
/* express::cookieParser: the cookie-parser package for Express++ (signatures match Node's
   cookie-parser / cookie-signature, so a Node app and an Express++ app can read each other's cookies).

     app.use(express::cookieParser());                                  // req.cookies() only
     app.use(express::cookieParser(express::env::get("COOKIE_SECRET")));
     app.use(express::cookieParser({"new-secret", "old-secret"}));      // rotation: sign with the first,
                                                                        // accept any of them
     res.cookie("uid", "42", {.sign = true});      // Set-Cookie: uid=s%3A42.<base64 HMAC-SHA256>
     auto uid = req.signedCookie("uid");           // std::optional<std::string>; nullopt if tampered
     auto all = req.signedCookies();               // valid signed cookies
     auto plain = req.cookies();                   // unsigned cookies, URL-decoded

   Helpers (same as Node's cookie-signature):
     express::cookieSign("42", secret)       -> "42.<sig>"
     express::cookieUnsign("42.<sig>", secret) -> "42" (std::nullopt if the signature is wrong)       */

#include <initializer_list>
#include <optional>
#include <string>
#include <vector>

#include "app.hpp"

namespace express {

Middleware cookieParser(std::vector<std::string> secrets = {});
inline Middleware cookieParser(const std::string &secret) { return cookieParser(std::vector<std::string>{secret}); }
inline Middleware cookieParser(std::initializer_list<std::string> secrets) {
    return cookieParser(std::vector<std::string>(secrets));
}
inline Middleware cookieParser(const char *secret) { return cookieParser(std::string(secret)); }

std::string cookieSign(const std::string &value, const std::string &secret);
std::optional<std::string> cookieUnsign(const std::string &signedValue, const std::string &secret);

}  // namespace express
