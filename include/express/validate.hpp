#pragma once
/* express::validate: joi / zod style validation for Express++.

     namespace v = express::v;
     auto signup = v::object({
         {"email",    v::string().trim().lowercase().email().required()},
         {"password", v::string().min(8).max(72).required()},
         {"age",      v::integer().min(13).max(120)},
         {"role",     v::string().oneOf({"user", "admin"}).defaultTo("user")},
         {"tags",     v::array(v::string().max(20)).max(5)},
     });

     app.post("/signup", express::validate({.body = signup}), [](express::Req req, express::Res res) {
         const express::Json &body = express::validated(req, "body");   // cleaned: trimmed, defaults filled,
         ...                                                             // unknown keys removed
     });

     app.get("/items", express::validate({.query = v::object({{"page", v::integer().min(1).defaultTo(1)}})}), ...);
     // query / params are strings: "2" is turned into 2 for integer(), "true" into true for boolean()

   On failure: 400 {"error": "Validation failed", "errors": {"email": "must be a valid email", "tags.1": "..."}}
   Standalone: auto r = signup.validate(json); if (!r.ok) ... r.errors ... r.value

   Rules
     all:      required() optional() nullable() defaultTo(x) oneOf({...}) custom(fn) message("...")
     string(): min(n) max(n) length(n) pattern(std::regex, "name") email() url() uuid() trim() lowercase() uppercase()
               nonEmpty()
     number() / integer(): min(x) max(x) positive() negative()
     array(items): min(n) max(n) length(n) unique()
     object(fields): strict() (unknown keys are errors) passthrough() (keep them); default: remove unknown keys
     boolean(), any()                                                                                        */

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <regex>
#include <string>
#include <utility>
#include <vector>

#include "app.hpp"

namespace express::v {

class Schema;

struct Result {
    bool ok = true;
    Json value;                                  // cleaned value (defaults, trim, coercion, unknown keys removed)
    std::map<std::string, std::string> errors;   // "email" -> "must be a valid email"; "" = the value itself
};

struct ValidateOptions {
    bool coerce = false;      // "42" -> 42, "true" -> true, "a,b" -> ["a","b"] (on for query / params)
    bool abortEarly = false;  // stop at the first error
};

namespace detail {
struct Node;
}

class Schema {
  public:
    enum class Kind { Any, String, Number, Integer, Boolean, Array, Object };

    explicit Schema(Kind k);

    Schema required() const;
    Schema optional() const;
    Schema nullable() const;
    Schema defaultTo(Json value) const;
    Schema oneOf(std::vector<Json> values) const;
    Schema message(std::string text) const;  // replace every error message of this field
    // custom(fn): fn(value) returns "" when ok, otherwise the error message; may change the value
    Schema custom(std::function<std::string(Json &)> fn) const;

    Schema min(double n) const;
    Schema max(double n) const;
    Schema length(size_t n) const;
    Schema positive() const;
    Schema negative() const;

    Schema pattern(const std::regex &re, std::string name = "pattern") const;
    Schema email() const;
    Schema url() const;
    Schema uuid() const;
    Schema trim() const;
    Schema lowercase() const;
    Schema uppercase() const;
    Schema nonEmpty() const { return min(1); }

    Schema unique() const;
    Schema strict() const;
    Schema passthrough() const;

    Result validate(const Json &input, const ValidateOptions &o = {}) const;

    const std::shared_ptr<detail::Node> &node() const { return n_; }

  private:
    Schema clone() const;
    std::shared_ptr<detail::Node> n_;
    friend Schema array(Schema);
    friend Schema object(std::vector<std::pair<std::string, Schema>>);
};

Schema any();
Schema string();
Schema number();
Schema integer();
Schema boolean();
Schema array(Schema items = any());
Schema object(std::vector<std::pair<std::string, Schema>> fields = {});

}  // namespace express::v

namespace express {

struct ValidateSpec {
    std::optional<v::Schema> body, query, params;
};

// 400 with {"error": "Validation failed", "errors": {...}} when something is wrong
Middleware validate(ValidateSpec spec);

// cleaned value stored by validate(): which = "body" | "query" | "params" (null Json if not validated)
const Json &validated(const Req &req, const std::string &which = "body");

}  // namespace express
