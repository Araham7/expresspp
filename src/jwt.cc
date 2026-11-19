#include "express/jwt.hpp"

#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/pem.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <memory>
#include <stdexcept>

namespace express::jwt {

namespace {

const char *B64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

std::string b64url(const std::string &in) {
    std::string out;
    size_t i = 0;
    while (i + 2 < in.size()) {
        unsigned n = (static_cast<unsigned char>(in[i]) << 16) | (static_cast<unsigned char>(in[i + 1]) << 8) |
                     static_cast<unsigned char>(in[i + 2]);
        out += B64[(n >> 18) & 63];
        out += B64[(n >> 12) & 63];
        out += B64[(n >> 6) & 63];
        out += B64[n & 63];
        i += 3;
    }
    if (i + 1 == in.size()) {
        unsigned n = static_cast<unsigned char>(in[i]) << 16;
        out += B64[(n >> 18) & 63];
        out += B64[(n >> 12) & 63];
    } else if (i + 2 == in.size()) {
        unsigned n = (static_cast<unsigned char>(in[i]) << 16) | (static_cast<unsigned char>(in[i + 1]) << 8);
        out += B64[(n >> 18) & 63];
        out += B64[(n >> 12) & 63];
        out += B64[(n >> 6) & 63];
    }
    return out;
}

bool unb64url(const std::string &in, std::string &out) {
    out.clear();
    unsigned buf = 0;
    int bits = 0;
    for (char c : in) {
        int v;
        if (c >= 'A' && c <= 'Z') v = c - 'A';
        else if (c >= 'a' && c <= 'z') v = c - 'a' + 26;
        else if (c >= '0' && c <= '9') v = c - '0' + 52;
        else if (c == '-' || c == '+') v = 62;
        else if (c == '_' || c == '/') v = 63;
        else if (c == '=') break;
        else return false;
        buf = (buf << 6) | static_cast<unsigned>(v);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out += static_cast<char>((buf >> bits) & 0xFF);
        }
    }
    return true;
}

long long now() {
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

const EVP_MD *hmacMd(const std::string &alg) {
    if (alg == "HS256") return EVP_sha256();
    if (alg == "HS384") return EVP_sha384();
    if (alg == "HS512") return EVP_sha512();
    return nullptr;
}

std::string hmac(const std::string &alg, const std::string &key, const std::string &data) {
    unsigned char out[EVP_MAX_MD_SIZE];
    unsigned int len = 0;
    if (!HMAC(hmacMd(alg), key.data(), static_cast<int>(key.size()), reinterpret_cast<const unsigned char *>(data.data()),
              data.size(), out, &len))
        throw std::runtime_error("Express++ jwt: HMAC failed");
    return std::string(reinterpret_cast<char *>(out), len);
}

struct BioDel { void operator()(BIO *b) const { BIO_free(b); } };
struct KeyDel { void operator()(EVP_PKEY *k) const { EVP_PKEY_free(k); } };
struct CtxDel { void operator()(EVP_MD_CTX *c) const { EVP_MD_CTX_free(c); } };

std::unique_ptr<EVP_PKEY, KeyDel> loadKey(const std::string &pem, bool priv) {
    std::unique_ptr<BIO, BioDel> bio(BIO_new_mem_buf(pem.data(), static_cast<int>(pem.size())));
    EVP_PKEY *k = priv ? PEM_read_bio_PrivateKey(bio.get(), nullptr, nullptr, nullptr)
                       : PEM_read_bio_PUBKEY(bio.get(), nullptr, nullptr, nullptr);
    if (!k && !priv) {  // maybe a certificate
        BIO_reset(bio.get());
        if (X509 *cert = PEM_read_bio_X509(bio.get(), nullptr, nullptr, nullptr)) {
            k = X509_get_pubkey(cert);
            X509_free(cert);
        }
    }
    if (!k) throw std::invalid_argument(priv ? "Express++ jwt: RS256 needs a PEM private key"
                                             : "Express++ jwt: RS256 needs a PEM public key");
    return std::unique_ptr<EVP_PKEY, KeyDel>(k);
}

std::string rsSign(const std::string &pem, const std::string &data) {
    auto key = loadKey(pem, true);
    std::unique_ptr<EVP_MD_CTX, CtxDel> ctx(EVP_MD_CTX_new());
    size_t len = 0;
    if (EVP_DigestSignInit(ctx.get(), nullptr, EVP_sha256(), nullptr, key.get()) != 1 ||
        EVP_DigestSign(ctx.get(), nullptr, &len, reinterpret_cast<const unsigned char *>(data.data()), data.size()) != 1)
        throw std::runtime_error("Express++ jwt: RS256 sign failed");
    std::string sig(len, '\0');
    if (EVP_DigestSign(ctx.get(), reinterpret_cast<unsigned char *>(sig.data()), &len,
                       reinterpret_cast<const unsigned char *>(data.data()), data.size()) != 1)
        throw std::runtime_error("Express++ jwt: RS256 sign failed");
    sig.resize(len);
    return sig;
}

bool rsVerify(const std::string &pem, const std::string &data, const std::string &sig) {
    std::unique_ptr<EVP_PKEY, KeyDel> key;
    try {
        key = loadKey(pem, false);
    } catch (const std::invalid_argument &e) {
        throw Error("secret or public key must be provided");
    }
    std::unique_ptr<EVP_MD_CTX, CtxDel> ctx(EVP_MD_CTX_new());
    if (EVP_DigestVerifyInit(ctx.get(), nullptr, EVP_sha256(), nullptr, key.get()) != 1) return false;
    return EVP_DigestVerify(ctx.get(), reinterpret_cast<const unsigned char *>(sig.data()), sig.size(),
                            reinterpret_cast<const unsigned char *>(data.data()), data.size()) == 1;
}

struct Parts {
    std::string h64, p64, s64;
    Json header, payload;
};

Parts split(const std::string &token) {
    if (token.empty()) throw Error("jwt must be provided");
    auto d1 = token.find('.');
    auto d2 = d1 == std::string::npos ? std::string::npos : token.find('.', d1 + 1);
    if (d2 == std::string::npos || token.find('.', d2 + 1) != std::string::npos) throw Error("jwt malformed");
    Parts p{token.substr(0, d1), token.substr(d1 + 1, d2 - d1 - 1), token.substr(d2 + 1), {}, {}};
    std::string h, pl;
    if (!unb64url(p.h64, h) || !unb64url(p.p64, pl)) throw Error("jwt malformed");
    try {
        p.header = parseJson(h);
        p.payload = parseJson(pl);
    } catch (...) {
        throw Error("jwt malformed");
    }
    if (!p.header.isObject()) throw Error("jwt malformed");
    return p;
}

}  // namespace

long long parseDuration(const std::string &text) {
    std::string s;
    for (char c : text)
        if (!std::isspace(static_cast<unsigned char>(c))) s += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (s.empty()) throw std::invalid_argument("Express++: empty duration");
    size_t i = 0;
    while (i < s.size() && (std::isdigit(static_cast<unsigned char>(s[i])) || s[i] == '.')) i++;
    if (i == 0) throw std::invalid_argument("Express++: bad duration '" + text + "'");
    double n = std::stod(s.substr(0, i));
    std::string u = s.substr(i);
    double mul;
    if (u.empty() || u == "s" || u == "sec" || u == "secs" || u == "second" || u == "seconds") mul = 1;
    else if (u == "m" || u == "min" || u == "mins" || u == "minute" || u == "minutes") mul = 60;
    else if (u == "h" || u == "hr" || u == "hrs" || u == "hour" || u == "hours") mul = 3600;
    else if (u == "d" || u == "day" || u == "days") mul = 86400;
    else if (u == "w" || u == "week" || u == "weeks") mul = 604800;
    else if (u == "y" || u == "yr" || u == "yrs" || u == "year" || u == "years") mul = 31557600;
    else throw std::invalid_argument("Express++: bad duration unit in '" + text + "'");
    return static_cast<long long>(n * mul);
}

std::string sign(const Json &payload, const std::string &key, const SignOptions &o) {
    if (o.algorithm == "none") throw std::invalid_argument("Express++ jwt: algorithm \"none\" is not allowed");
    bool hs = hmacMd(o.algorithm) != nullptr;
    if (!hs && o.algorithm != "RS256") throw std::invalid_argument("Express++ jwt: unsupported algorithm " + o.algorithm);
    if (key.empty()) throw std::invalid_argument("Express++ jwt: secretOrPrivateKey must have a value");
    if (!payload.isObject()) throw std::invalid_argument("Express++ jwt: payload must be an object");

    Json header = obj("alg", o.algorithm, "typ", "JWT");
    if (!o.keyid.empty()) header["kid"] = o.keyid;
    Json p = payload;
    long long t = now();
    if (!o.noTimestamp && !p.isMember("iat")) p["iat"] = static_cast<Json::Int64>(t);
    long long base = p.isMember("iat") && p["iat"].isIntegral() ? p["iat"].asInt64() : t;
    if (!o.expiresIn.empty()) p["exp"] = static_cast<Json::Int64>(base + parseDuration(o.expiresIn));
    if (!o.notBefore.empty()) p["nbf"] = static_cast<Json::Int64>(base + parseDuration(o.notBefore));
    if (!o.issuer.empty()) p["iss"] = o.issuer;
    if (!o.subject.empty()) p["sub"] = o.subject;
    if (!o.audience.empty()) p["aud"] = o.audience;
    if (!o.jwtid.empty()) p["jti"] = o.jwtid;

    std::string input = b64url(stringify(header)) + "." + b64url(stringify(p));
    std::string sig = hs ? hmac(o.algorithm, key, input) : rsSign(key, input);
    return input + "." + b64url(sig);
}

Json decode(const std::string &token) { return split(token).payload; }
Json decodeHeader(const std::string &token) { return split(token).header; }

Json verify(const std::string &token, const std::string &key, const VerifyOptions &o) {
    Parts p = split(token);
    std::string alg = p.header.get("alg", "").asString();
    if (alg.empty() || alg == "none") throw Error("jwt signature is required");
    if (std::find(o.algorithms.begin(), o.algorithms.end(), alg) == o.algorithms.end()) throw Error("invalid algorithm");
    if (key.empty()) throw Error("secret or public key must be provided");

    std::string sig;
    if (!unb64url(p.s64, sig)) throw Error("invalid signature");
    std::string input = p.h64 + "." + p.p64;
    bool ok;
    if (hmacMd(alg)) {
        std::string expect = hmac(alg, key, input);
        ok = expect.size() == sig.size() && CRYPTO_memcmp(expect.data(), sig.data(), sig.size()) == 0;
    } else if (alg == "RS256") {
        ok = rsVerify(key, input, sig);
    } else {
        throw Error("invalid algorithm");
    }
    if (!ok) throw Error("invalid signature");

    const Json &pl = p.payload;
    if (!pl.isObject()) throw Error("jwt payload must be an object");
    long long t = now();
    if (pl.isMember("nbf")) {
        if (!pl["nbf"].isNumeric()) throw Error("invalid nbf value");
        if (pl["nbf"].asInt64() > t + o.clockTolerance) throw NotBeforeError(pl["nbf"].asInt64());
    }
    if (pl.isMember("exp") && !o.ignoreExpiration) {
        if (!pl["exp"].isNumeric()) throw Error("invalid exp value");
        if (t >= pl["exp"].asInt64() + o.clockTolerance) throw TokenExpiredError(pl["exp"].asInt64());
    }
    if (!o.audience.empty()) {
        bool match = false;
        const Json &aud = pl["aud"];
        if (aud.isString()) match = aud.asString() == o.audience;
        else if (aud.isArray())
            for (const auto &a : aud) match = match || (a.isString() && a.asString() == o.audience);
        if (!match) throw Error("jwt audience invalid. expected: " + o.audience);
    }
    if (!o.issuer.empty() && pl.get("iss", "").asString() != o.issuer) throw Error("jwt issuer invalid. expected: " + o.issuer);
    if (!o.subject.empty() && pl.get("sub", "").asString() != o.subject) throw Error("jwt subject invalid. expected: " + o.subject);
    if (!o.maxAge.empty()) {
        if (!pl["iat"].isNumeric()) throw Error("iat required when maxAge is specified");
        long long maxAge = parseDuration(o.maxAge);
        if (t - o.clockTolerance > pl["iat"].asInt64() + maxAge) throw TokenExpiredError(pl["iat"].asInt64() + maxAge);
    }
    return pl;
}

const Json &user(const Req &req, const std::string &property) {
    static const Json null;
    if (!req.has(property)) return null;
    return req.get<Json>(property);
}

}  // namespace express::jwt

namespace express {

Middleware auth(AuthOptions o) {
    if (o.secret.empty()) throw std::invalid_argument("Express++ auth: secret is required");
    return [o](Req req, Res, Next next) {
        std::string token;
        if (o.getToken) {
            token = o.getToken(req);
        } else {
            const std::string &h = req.header("authorization");
            if (!h.empty()) {
                auto sp = h.find(' ');
                std::string scheme = h.substr(0, sp);
                std::transform(scheme.begin(), scheme.end(), scheme.begin(), ::tolower);
                if (sp == std::string::npos || scheme != "bearer")
                    throw jwt::Error("Format is Authorization: Bearer [token]");
                token = h.substr(sp + 1);
                while (!token.empty() && token.front() == ' ') token.erase(token.begin());
            } else if (!o.cookie.empty()) {
                token = req.cookie(o.cookie);
            }
        }
        if (token.empty()) {
            if (!o.credentialsRequired) return next();
            throw jwt::Error("No authorization token was found");
        }
        jwt::VerifyOptions vo;
        vo.algorithms = o.algorithms;
        vo.issuer = o.issuer;
        vo.audience = o.audience;
        Json payload = jwt::verify(token, o.secret, vo);
        if (o.isRevoked && o.isRevoked(req, payload)) throw jwt::Error("The token has been revoked.");
        req.set<Json>(o.property, std::move(payload));
        next();
    };
}

}  // namespace express
