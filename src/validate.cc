#include "express/validate.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <set>
#include <sstream>

namespace express::v {

namespace detail {
struct Node {
    Schema::Kind kind;
    bool required = false, nullable = false, hasDefault = false;
    Json def;
    std::vector<Json> oneOf;
    std::string message;
    std::vector<std::function<std::string(Json &)>> custom;
    std::optional<double> min, max;
    std::optional<size_t> length;
    bool positive = false, negative = false;
    std::vector<std::pair<std::shared_ptr<std::regex>, std::string>> patterns;
    bool email = false, url = false, uuid = false, trim = false, lower = false, upper = false;
    bool unique = false;
    std::string unknown = "strip";  // strip | strict | passthrough
    std::shared_ptr<Node> items;
    std::vector<std::pair<std::string, std::shared_ptr<Node>>> fields;
};
}  // namespace detail

using detail::Node;

Schema::Schema(Kind k) : n_(std::make_shared<Node>()) { n_->kind = k; }

Schema Schema::clone() const {
    Schema s(n_->kind);
    s.n_ = std::make_shared<Node>(*n_);
    return s;
}

#define EXPRESS_V_SET(stmt) \
    Schema s = clone();     \
    auto &n = *s.n_;        \
    stmt;                   \
    return s;

Schema Schema::required() const { EXPRESS_V_SET(n.required = true) }
Schema Schema::optional() const { EXPRESS_V_SET(n.required = false) }
Schema Schema::nullable() const { EXPRESS_V_SET(n.nullable = true) }
Schema Schema::defaultTo(Json value) const { EXPRESS_V_SET(n.hasDefault = true; n.def = std::move(value)) }
Schema Schema::oneOf(std::vector<Json> values) const { EXPRESS_V_SET(n.oneOf = std::move(values)) }
Schema Schema::message(std::string text) const { EXPRESS_V_SET(n.message = std::move(text)) }
Schema Schema::custom(std::function<std::string(Json &)> fn) const { EXPRESS_V_SET(n.custom.push_back(std::move(fn))) }
Schema Schema::min(double v) const { EXPRESS_V_SET(n.min = v) }
Schema Schema::max(double v) const { EXPRESS_V_SET(n.max = v) }
Schema Schema::length(size_t v) const { EXPRESS_V_SET(n.length = v) }
Schema Schema::positive() const { EXPRESS_V_SET(n.positive = true) }
Schema Schema::negative() const { EXPRESS_V_SET(n.negative = true) }
Schema Schema::pattern(const std::regex &re, std::string name) const {
    EXPRESS_V_SET(n.patterns.emplace_back(std::make_shared<std::regex>(re), std::move(name)))
}
Schema Schema::email() const { EXPRESS_V_SET(n.email = true) }
Schema Schema::url() const { EXPRESS_V_SET(n.url = true) }
Schema Schema::uuid() const { EXPRESS_V_SET(n.uuid = true) }
Schema Schema::trim() const { EXPRESS_V_SET(n.trim = true) }
Schema Schema::lowercase() const { EXPRESS_V_SET(n.lower = true) }
Schema Schema::uppercase() const { EXPRESS_V_SET(n.upper = true) }
Schema Schema::unique() const { EXPRESS_V_SET(n.unique = true) }
Schema Schema::strict() const { EXPRESS_V_SET(n.unknown = "strict") }
Schema Schema::passthrough() const { EXPRESS_V_SET(n.unknown = "passthrough") }

Schema any() { return Schema(Schema::Kind::Any); }
Schema string() { return Schema(Schema::Kind::String); }
Schema number() { return Schema(Schema::Kind::Number); }
Schema integer() { return Schema(Schema::Kind::Integer); }
Schema boolean() { return Schema(Schema::Kind::Boolean); }
Schema array(Schema items) {
    Schema s(Schema::Kind::Array);
    s.n_->items = items.n_;
    return s;
}
Schema object(std::vector<std::pair<std::string, Schema>> fields) {
    Schema s(Schema::Kind::Object);
    for (auto &f : fields) s.n_->fields.emplace_back(f.first, f.second.n_);
    return s;
}

namespace {

std::string num(double d) {
    std::ostringstream os;
    os << d;
    return os.str();
}

std::string show(const Json &v) { return v.isString() ? v.asString() : stringify(v); }

std::string join(const std::string &base, const std::string &key) { return base.empty() ? key : base + "." + key; }

const std::regex &emailRe() {
    static const std::regex re(R"(^[A-Za-z0-9.!#$%&'*+/=?^_`{|}~-]+@[A-Za-z0-9](?:[A-Za-z0-9-]{0,61}[A-Za-z0-9])?(?:\.[A-Za-z0-9](?:[A-Za-z0-9-]{0,61}[A-Za-z0-9])?)+$)");
    return re;
}
const std::regex &urlRe() {
    static const std::regex re(R"(^https?://[^\s/$.?#][^\s]*$)", std::regex::icase);
    return re;
}
const std::regex &uuidRe() {
    static const std::regex re(R"(^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}$)");
    return re;
}

struct Ctx {
    const ValidateOptions &o;
    std::map<std::string, std::string> &errors;
    bool stop() const { return o.abortEarly && !errors.empty(); }
};

// returns false if the field should be left out of the output
bool check(const Node &n, Json &v, bool present, const std::string &path, Ctx &c) {
    auto fail = [&](const std::string &msg) {
        if (!c.errors.count(path)) c.errors[path] = n.message.empty() ? msg : n.message;
    };
    if (!present || (v.isNull() && !n.nullable)) {
        if (n.hasDefault) {
            v = n.def;
            return true;
        }
        if (n.required) fail(present ? "must not be null" : "is required");
        return false;
    }
    if (v.isNull()) return true;  // nullable

    // coercion for strings coming from query / params / forms
    if (c.o.coerce && v.isString()) {
        std::string s = v.asString();
        if (n.kind == Schema::Kind::Integer || n.kind == Schema::Kind::Number) {
            try {
                size_t used = 0;
                double d = std::stod(s, &used);
                if (used == s.size() && !s.empty()) {
                    if (n.kind == Schema::Kind::Integer && std::floor(d) == d && std::fabs(d) < 9e15)
                        v = Json(static_cast<Json::Int64>(d));
                    else
                        v = Json(d);
                }
            } catch (...) {
            }
        } else if (n.kind == Schema::Kind::Boolean) {
            std::string l = s;
            std::transform(l.begin(), l.end(), l.begin(), ::tolower);
            if (l == "true" || l == "1" || l == "yes" || l == "on") v = Json(true);
            else if (l == "false" || l == "0" || l == "no" || l == "off") v = Json(false);
        } else if (n.kind == Schema::Kind::Array) {
            Json a(::Json::arrayValue);
            size_t i = 0;
            while (i <= s.size() && !s.empty()) {
                size_t j = s.find(',', i);
                if (j == std::string::npos) j = s.size();
                a.append(s.substr(i, j - i));
                i = j + 1;
            }
            v = a;
        }
    }

    switch (n.kind) {
        case Schema::Kind::Any:
            break;
        case Schema::Kind::String: {
            if (!v.isString()) {
                fail("must be a string");
                return true;
            }
            std::string s = v.asString();
            if (n.trim) {
                size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
                s = a == std::string::npos ? "" : s.substr(a, b - a + 1);
            }
            if (n.lower) std::transform(s.begin(), s.end(), s.begin(), ::tolower);
            if (n.upper) std::transform(s.begin(), s.end(), s.begin(), ::toupper);
            v = s;
            if (s.empty() && n.required && !n.min) {
                fail("is not allowed to be empty");
                return true;
            }
            if (n.length && s.size() != *n.length) fail("must be exactly " + std::to_string(*n.length) + " characters");
            if (n.min && s.size() < *n.min) fail("must be at least " + num(*n.min) + " characters");
            if (n.max && s.size() > *n.max) fail("must be at most " + num(*n.max) + " characters");
            if (n.email && !std::regex_match(s, emailRe())) fail("must be a valid email");
            if (n.url && !std::regex_match(s, urlRe())) fail("must be a valid URL");
            if (n.uuid && !std::regex_match(s, uuidRe())) fail("must be a valid UUID");
            for (const auto &[re, name] : n.patterns)
                if (!std::regex_search(s, *re)) fail("must match " + name);
            break;
        }
        case Schema::Kind::Number:
        case Schema::Kind::Integer: {
            if (!v.isNumeric() || v.isBool()) {
                fail(n.kind == Schema::Kind::Integer ? "must be an integer" : "must be a number");
                return true;
            }
            double d = v.asDouble();
            if (n.kind == Schema::Kind::Integer && std::floor(d) != d) {
                fail("must be an integer");
                return true;
            }
            if (n.min && d < *n.min) fail("must be greater than or equal to " + num(*n.min));
            if (n.max && d > *n.max) fail("must be less than or equal to " + num(*n.max));
            if (n.positive && d <= 0) fail("must be a positive number");
            if (n.negative && d >= 0) fail("must be a negative number");
            break;
        }
        case Schema::Kind::Boolean:
            if (!v.isBool()) {
                fail("must be a boolean");
                return true;
            }
            break;
        case Schema::Kind::Array: {
            if (!v.isArray()) {
                fail("must be an array");
                return true;
            }
            if (n.length && v.size() != *n.length) fail("must contain exactly " + std::to_string(*n.length) + " items");
            if (n.min && v.size() < *n.min) fail("must contain at least " + num(*n.min) + " items");
            if (n.max && v.size() > *n.max) fail("must contain at most " + num(*n.max) + " items");
            if (n.items) {
                Json out(::Json::arrayValue);
                for (Json::ArrayIndex i = 0; i < v.size() && !c.stop(); i++) {
                    Json item = v[i];
                    check(*n.items, item, true, join(path, std::to_string(i)), c);
                    out.append(item);
                }
                v = out;
            }
            if (n.unique) {
                std::set<std::string> seen;
                for (const auto &item : v)
                    if (!seen.insert(stringify(item)).second) {
                        fail("must not contain duplicate items");
                        break;
                    }
            }
            break;
        }
        case Schema::Kind::Object: {
            if (!v.isObject()) {
                fail("must be an object");
                return true;
            }
            Json out(::Json::objectValue);
            std::set<std::string> known;
            for (const auto &[key, child] : n.fields) {
                known.insert(key);
                if (c.stop()) break;
                bool has = v.isMember(key);
                Json fv = has ? v[key] : Json();
                if (check(*child, fv, has, join(path, key), c)) out[key] = fv;
            }
            for (const auto &key : v.getMemberNames()) {
                if (known.count(key)) continue;
                if (n.unknown == "strict")
                    c.errors.emplace(join(path, key), "is not allowed");
                else if (n.unknown == "passthrough" || n.fields.empty())
                    out[key] = v[key];
            }
            v = out;
            break;
        }
    }
    if (!n.oneOf.empty() && std::find(n.oneOf.begin(), n.oneOf.end(), v) == n.oneOf.end()) {
        std::string list;
        for (const auto &o : n.oneOf) list += (list.empty() ? "" : ", ") + show(o);
        fail("must be one of [" + list + "]");
    }
    for (const auto &fn : n.custom) {
        std::string err = fn(v);
        if (!err.empty()) fail(err);
    }
    return true;
}

}  // namespace

Result Schema::validate(const Json &input, const ValidateOptions &o) const {
    Result r;
    r.value = input;
    Ctx c{o, r.errors};
    bool present = !input.isNull() || n_->nullable;
    if (!check(*n_, r.value, present, "", c)) r.value = Json();
    // a missing body / query is checked as {} so required fields report themselves
    if (!present && n_->kind == Schema::Kind::Object && !n_->hasDefault && r.errors.empty()) {
        r.value = Json(::Json::objectValue);
        check(*n_, r.value, true, "", c);
    }
    r.ok = r.errors.empty();
    return r;
}

}  // namespace express::v

namespace express {

namespace {
const char *KEY_BODY = "express.valid.body";
const char *KEY_QUERY = "express.valid.query";
const char *KEY_PARAMS = "express.valid.params";

Json toJson(const auto &map) {
    Json o(::Json::objectValue);
    for (const auto &[k, val] : map) o[k] = val;
    return o;
}
}  // namespace

Middleware validate(ValidateSpec spec) {
    return [spec](Req req, Res, Next next) {
        std::map<std::string, std::string> errors;
        auto run = [&](const std::optional<v::Schema> &s, const Json &input, bool coerce, const char *key) {
            if (!s) return;
            auto r = s->validate(input, {.coerce = coerce});
            for (auto &[k, msg] : r.errors) errors.emplace(k.empty() ? "value" : k, msg);
            req.set<Json>(key, std::move(r.value));
        };
        const Json &body = req.json();
        run(spec.body, body.isNull() ? Json(::Json::objectValue) : body, false, KEY_BODY);
        if (spec.query) run(spec.query, toJson(req.queries()), true, KEY_QUERY);
        if (spec.params) run(spec.params, toJson(req.params()), true, KEY_PARAMS);
        if (!errors.empty()) {
            Json e(::Json::objectValue);
            for (auto &[k, msg] : errors) e[k] = msg;
            throw HttpError(400, "Validation failed", obj("errors", e));
        }
        next();
    };
}

const Json &validated(const Req &req, const std::string &which) {
    static const Json null;
    std::string key = which == "query" ? KEY_QUERY : which == "params" ? KEY_PARAMS : KEY_BODY;
    if (!req.has(key)) return null;
    return req.get<Json>(key);
}

}  // namespace express
