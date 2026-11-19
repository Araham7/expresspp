#pragma once
/* express::jwt: jsonwebtoken + express-jwt for Express++.
   Tokens are standard JWTs: a token made here verifies in Node's jsonwebtoken and the other way round.

     auto token = express::jwt::sign(express::obj("id", 7, "role", "admin"), secret, {.expiresIn = "7d"});
     express::Json p = express::jwt::verify(token, secret);         // throws jwt::Error (a 401 HttpError)
     express::Json p = express::jwt::decode(token);                 // no checks (just read it)

     app.use("/api", express::auth({.secret = secret}));             // express-jwt: Bearer token -> req user
     app.get("/api/me", [](express::Req req, express::Res res) {
         res.json(express::jwt::user(req));                          // the verified payload
     });

   Algorithms: HS256 (default), HS384, HS512 with a shared secret; RS256 with a PEM private key for sign()
   and a PEM public key for verify(). "none" is always rejected.
   expiresIn / notBefore: "60" (seconds) "30s" "15m" "12h" "7d" "2w" "1y", or a number of seconds.

   Errors (all are express::HttpError with status 401, so an uncaught one becomes a 401 response):
     jwt::TokenExpiredError  "jwt expired"          (expiredAt())
     jwt::NotBeforeError     "jwt not active"
     jwt::Error              "invalid signature", "jwt malformed", "jwt audience invalid. expected: x", ... */

#include <functional>
#include <string>
#include <vector>

#include "app.hpp"

namespace express::jwt {

class Error : public HttpError {
  public:
    explicit Error(const std::string &m) : HttpError(401, m) {}
};
class TokenExpiredError : public Error {
  public:
    TokenExpiredError(long long expiredAt) : Error("jwt expired"), expiredAt_(expiredAt) {}
    long long expiredAt() const { return expiredAt_; }  // unix seconds

  private:
    long long expiredAt_;
};
class NotBeforeError : public Error {
  public:
    NotBeforeError(long long date) : Error("jwt not active"), date_(date) {}
    long long date() const { return date_; }

  private:
    long long date_;
};

struct SignOptions {
    std::string algorithm = "HS256";
    std::string expiresIn;   // "" = never
    std::string notBefore;   // "" = valid now
    std::string issuer, subject, audience, jwtid, keyid;
    bool noTimestamp = false;  // don't add "iat"
};

struct VerifyOptions {
    std::vector<std::string> algorithms{"HS256"};
    std::string issuer, subject, audience;
    long long clockTolerance = 0;  // seconds of slack for exp / nbf
    std::string maxAge;            // reject tokens whose iat is older than this ("1d")
    bool ignoreExpiration = false;
};

// secretOrKey: shared secret (HS*) or PEM private key (RS256)
std::string sign(const Json &payload, const std::string &secretOrKey, const SignOptions &o = {});
// secretOrKey: shared secret (HS*) or PEM public key (RS256); returns the payload
Json verify(const std::string &token, const std::string &secretOrKey, const VerifyOptions &o = {});
// payload without any checks; throws jwt::Error if it isn't a JWT at all
Json decode(const std::string &token);
Json decodeHeader(const std::string &token);

// "7d" -> 604800 (seconds). Throws std::invalid_argument.
long long parseDuration(const std::string &text);

// the payload express::auth() stored on the request (null Json if none)
const Json &user(const Req &req, const std::string &property = "user");

}  // namespace express::jwt

namespace express {

struct AuthOptions {
    std::string secret;  // HS* secret, or the PEM public key for RS256
    std::vector<std::string> algorithms{"HS256"};
    bool credentialsRequired = true;  // false: no token -> next() without a user
    std::string cookie;               // also look for the token in this cookie
    std::string property = "user";    // req.get<express::Json>(property)
    std::string issuer, audience;
    std::function<std::string(const Req &)> getToken;  // your own token lookup
    std::function<bool(const Req &, const Json &)> isRevoked;  // return true to reject (logout lists)
};

// express-jwt: reads "Authorization: Bearer <token>" (or the cookie), verifies it, stores the payload
Middleware auth(AuthOptions o);

}  // namespace express
