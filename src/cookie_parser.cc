#include "express/cookie_parser.hpp"

#include "express/crypto.hpp"

namespace express {

std::string cookieSign(const std::string &value, const std::string &secret) {
    std::string sig = crypto::createHmac("sha256", secret).update(value).digest("base64");
    while (!sig.empty() && sig.back() == '=') sig.pop_back();
    return value + "." + sig;
}

std::optional<std::string> cookieUnsign(const std::string &signedValue, const std::string &secret) {
    auto dot = signedValue.rfind('.');
    if (dot == std::string::npos) return std::nullopt;
    std::string value = signedValue.substr(0, dot);
    if (!crypto::timingSafeEqual(cookieSign(value, secret), signedValue)) return std::nullopt;
    return value;
}

Middleware cookieParser(std::vector<std::string> secrets) {
    return [secrets](Req req, Res res, Next next) {
        auto &rs = req.state();
        if (rs.cookieParsed) {  // mounted twice: nothing to do
            next();
            return;
        }
        rs.cookieParsed = true;
        rs.cookieSecrets = secrets;
        if (!secrets.empty()) {
            std::string first = secrets.front();
            res.state().cookieSigner = [first](const std::string &v) { return cookieSign(v, first); };
        }
        for (const auto &[name, raw] : req.raw()->cookies()) {
            std::string v = raw;
            if (v.size() >= 2 && v.front() == '"' && v.back() == '"') v = v.substr(1, v.size() - 2);
            v = detail::uriDecode(v);
            if (secrets.empty() || v.rfind("s:", 0) != 0) continue;
            rs.signedNames.insert(name);
            for (const auto &s : secrets) {
                if (auto ok = cookieUnsign(v.substr(2), s)) {
                    rs.signedCookies[name] = *ok;
                    break;
                }
            }
        }
        next();
    };
}

}  // namespace express
