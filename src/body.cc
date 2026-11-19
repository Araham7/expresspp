#include "express/body.hpp"

#include <cctype>
#include <stdexcept>

namespace express {

size_t parseBytes(const std::string &text) {
    std::string s;
    for (char c : text)
        if (!std::isspace(static_cast<unsigned char>(c))) s += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (s.empty()) throw std::invalid_argument("Express++: empty size");
    size_t i = 0;
    while (i < s.size() && (std::isdigit(static_cast<unsigned char>(s[i])) || s[i] == '.')) i++;
    if (i == 0) throw std::invalid_argument("Express++: bad size '" + text + "'");
    double n = std::stod(s.substr(0, i));
    std::string unit = s.substr(i);
    double mul = 1;
    if (unit.empty() || unit == "b")
        mul = 1;
    else if (unit == "kb" || unit == "k")
        mul = 1024;
    else if (unit == "mb" || unit == "m")
        mul = 1024.0 * 1024;
    else if (unit == "gb" || unit == "g")
        mul = 1024.0 * 1024 * 1024;
    else
        throw std::invalid_argument("Express++: bad size unit in '" + text + "'");
    return static_cast<size_t>(n * mul);
}

Middleware bodyLimit(const std::string &limit) {
    size_t max = parseBytes(limit);
    return [max](Req req, Res, Next next) {
        if (req.raw()->body().size() > max) throw HttpError(413, "Payload Too Large");
        next();
    };
}

Middleware json(JsonOptions o) {
    size_t max = parseBytes(o.limit);
    return [o, max](Req req, Res, Next next) {
        if (!req.is(o.type) && !(o.type == "json" && req.is("+json"))) {
            next();
            return;
        }
        if (req.raw()->body().size() > max) throw HttpError(413, "Payload Too Large");
        Json value;
        if (auto p = req.raw()->getJsonObject()) {
            value = *p;
        } else {
            try {
                value = parseJson(req.raw()->body());
            } catch (const std::exception &) {
                std::string why = req.raw()->getJsonError();
                throw HttpError(400, "Invalid JSON" + (why.empty() ? std::string() : ": " + why));
            }
        }
        if (o.strict && !value.isObject() && !value.isArray())
            throw HttpError(400, "Invalid JSON: top level must be an object or array");
        next();
    };
}

}  // namespace express
