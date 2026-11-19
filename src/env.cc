#include "express/env.hpp"

#include <cctype>
#include <cstdlib>
#include <fstream>
#include <stdexcept>
#include <strings.h>

namespace express::env {

namespace {

std::string trim(const std::string &s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) a++;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) b--;
    return s.substr(a, b - a);
}

std::string expand(const std::string &v) {
    std::string out;
    for (size_t i = 0; i < v.size(); i++) {
        if (v[i] == '$' && i + 1 < v.size() && v[i + 1] == '{') {
            size_t close = v.find('}', i + 2);
            if (close != std::string::npos) {
                std::string name = v.substr(i + 2, close - i - 2);
                if (const char *val = std::getenv(name.c_str())) out += val;
                i = close;
                continue;
            }
        }
        out += v[i];
    }
    return out;
}

}  // namespace

int load(const std::string &path, bool override) {
    std::ifstream in(path);
    if (!in) return -1;
    int count = 0;
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::string t = trim(line);
        if (t.empty() || t[0] == '#') continue;
        if (t.rfind("export ", 0) == 0) t = trim(t.substr(7));
        size_t eq = t.find('=');
        if (eq == std::string::npos) continue;
        std::string key = trim(t.substr(0, eq));
        std::string raw = trim(t.substr(eq + 1));
        if (key.empty()) continue;

        std::string value;
        if (!raw.empty() && raw[0] == '"') {
            size_t i = 1;
            for (; i < raw.size() && raw[i] != '"'; i++) {
                if (raw[i] == '\\' && i + 1 < raw.size()) {
                    char c = raw[++i];
                    value += c == 'n' ? '\n' : c == 't' ? '\t' : c == 'r' ? '\r' : c;
                } else {
                    value += raw[i];
                }
            }
            value = expand(value);
        } else if (!raw.empty() && raw[0] == '\'') {
            size_t close = raw.find('\'', 1);
            value = raw.substr(1, close == std::string::npos ? std::string::npos : close - 1);
        } else {
            size_t hash = raw.find(" #");
            if (hash != std::string::npos) raw = trim(raw.substr(0, hash));
            value = expand(raw);
        }
        if (!override && std::getenv(key.c_str())) continue;
        setenv(key.c_str(), value.c_str(), 1);
        count++;
    }
    return count;
}

std::string get(const std::string &key, const std::string &def) {
    const char *v = std::getenv(key.c_str());
    return v && *v ? std::string(v) : def;  // missing or empty -> default (same as process.env["X"] || def)
}

bool has(const std::string &key) { return std::getenv(key.c_str()) != nullptr; }

long long getInt(const std::string &key, long long def) {
    const char *v = std::getenv(key.c_str());
    if (!v || !*v) return def;
    try {
        size_t used = 0;
        long long n = std::stoll(v, &used);
        if (used != std::string(v).size()) throw std::invalid_argument("x");
        return n;
    } catch (...) {
        throw std::runtime_error("express::env: " + key + " is not an integer: '" + v + "'");
    }
}

double getDouble(const std::string &key, double def) {
    const char *v = std::getenv(key.c_str());
    if (!v || !*v) return def;
    try {
        return std::stod(v);
    } catch (...) {
        throw std::runtime_error("express::env: " + key + " is not a number: '" + v + "'");
    }
}

bool getBool(const std::string &key, bool def) {
    const char *v = std::getenv(key.c_str());
    if (!v || !*v) return def;
    for (const char *t : {"1", "true", "yes", "on"})
        if (strcasecmp(v, t) == 0) return true;
    for (const char *f : {"0", "false", "no", "off"})
        if (strcasecmp(v, f) == 0) return false;
    throw std::runtime_error("express::env: " + key + " is not a boolean: '" + v + "'");
}

std::string require(const std::string &key) {
    const char *v = std::getenv(key.c_str());
    if (!v || !*v) throw std::runtime_error("express::env: missing required environment variable " + key);
    return v;
}

bool isProduction() { return get("EXPRESS_ENV", get("NODE_ENV")) == "production"; }

}  // namespace express::env

namespace express {

EnvValue::EnvValue(std::string key) : key_(std::move(key)) {
    if (const char *v = std::getenv(key_.c_str())) value_ = v;
}

EnvValue &EnvValue::operator=(const std::string &v) {
    setenv(key_.c_str(), v.c_str(), 1);
    value_ = v;
    return *this;
}

long long EnvValue::asInt(long long def) const { return env::getInt(key_, def); }
double EnvValue::asDouble(double def) const { return env::getDouble(key_, def); }
bool EnvValue::asBool(bool def) const { return env::getBool(key_, def); }

}  // namespace express
