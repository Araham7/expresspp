#pragma once
/* express::env: dotenv for C++ (no Drogon needed, link express::env).

     express::env::load();                              // reads ./.env (existing env vars win, like dotenv)
     express::env::load("config/.env.local", true);     // override = true: file wins
     int port   = express::env::getInt("PORT", 3000);
     auto uri   = express::env::get("MONGODB_URI", "memory://dev");
     auto key   = express::env::require("JWT_SECRET");  // throws if missing or empty
     bool debug = express::env::getBool("DEBUG", false);  // 1/true/yes/on

   .env syntax:
     # comment
     export NAME=value          (export is optional)
     QUOTED="line1\nline2"      (\n \t \" \\ in double quotes; ${VAR} expanded)
     RAW='no ${expansion} here'
     URL=${HOST}:${PORT}/api    (unquoted values are expanded too)
     KEY=value # inline comment (needs a space before #)                                         */

#include <string>

namespace express::env {

// Returns how many variables were set. Missing file: returns -1 (not an error, like dotenv).
int load(const std::string &path = ".env", bool override = false);

std::string get(const std::string &key, const std::string &def = "");  // missing or empty -> def
long long getInt(const std::string &key, long long def = 0);
double getDouble(const std::string &key, double def = 0);
bool getBool(const std::string &key, bool def = false);
std::string require(const std::string &key);
bool has(const std::string &key);
bool isProduction();  // EXPRESS_ENV or NODE_ENV == "production"

}  // namespace express::env

/* express::process.env: the Node.js way.

     auto url  = express::process.env["CLIENT_URL"] || "*";      // const url  = process.env.CLIENT_URL || "*"
     auto port = express::process.env["PORT"] || 3000;           // const port = process.env.PORT || 3000
     bool dbg  = express::process.env["DEBUG"] || false;         // 1/true/yes/on
     std::string secret = express::process.env["JWT_SECRET"];    // "" when missing
     express::process.env["MODE"] = "test";                      // process.env.MODE = "test"

   Like Node, a missing OR empty variable falls back to the default.
   (Unlike Node, both sides of || are evaluated: keep defaults simple values.)                     */

#include <optional>
#include <ostream>

namespace express {

class EnvValue {
  public:
    explicit EnvValue(std::string key);

    const std::string &key() const { return key_; }
    bool exists() const { return value_.has_value(); }              // set (maybe empty)
    bool empty() const { return !value_ || value_->empty(); }        // missing or ""
    std::string str() const { return value_.value_or(""); }
    operator std::string() const { return str(); }

    EnvValue &operator=(const std::string &v);  // process.env["X"] = "y"
    EnvValue &operator=(const char *v) { return *this = std::string(v); }

    // value || default
    friend std::string operator||(const EnvValue &e, const char *def) { return e.empty() ? std::string(def) : *e.value_; }
    friend std::string operator||(const EnvValue &e, const std::string &def) { return e.empty() ? def : *e.value_; }
    friend std::string operator||(const EnvValue &a, const EnvValue &b) { return a.empty() ? b.str() : *a.value_; }
    friend long long operator||(const EnvValue &e, int def) { return e.asInt(def); }
    friend long long operator||(const EnvValue &e, long def) { return e.asInt(def); }
    friend long long operator||(const EnvValue &e, long long def) { return e.asInt(def); }
    friend double operator||(const EnvValue &e, double def) { return e.asDouble(def); }
    friend bool operator||(const EnvValue &e, bool def) { return e.asBool(def); }

    friend bool operator==(const EnvValue &e, const std::string &s) { return e.str() == s; }
    friend bool operator==(const EnvValue &e, const char *s) { return e.str() == s; }
    friend std::ostream &operator<<(std::ostream &os, const EnvValue &e) { return os << e.str(); }

  private:
    long long asInt(long long def) const;  // throws std::runtime_error if set but not a number
    double asDouble(double def) const;
    bool asBool(bool def) const;
    std::string key_;
    std::optional<std::string> value_;
};

struct ProcessEnv {
    EnvValue operator[](const std::string &key) const { return EnvValue(key); }
};

struct Process {
    ProcessEnv env;
};

inline Process process;

}  // namespace express
