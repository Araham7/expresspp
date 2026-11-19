#include "express/hash.hpp"

#include <crypt.h>
#include <sodium.h>

#include <cstring>
#include <memory>
#include <stdexcept>

namespace express::hash {

namespace {
void initSodium() {
    static const int ok = sodium_init();
    if (ok < 0) throw std::runtime_error("Express++ hash: libsodium failed to initialise");
}

// "$argon2id$v=19$m=65536,t=3,p=1$..." -> m, t
bool argonParams(const std::string &h, unsigned long long &m, unsigned long long &t) {
    auto mp = h.find("$m=");
    auto tp = h.find(",t=");
    if (mp == std::string::npos || tp == std::string::npos) return false;
    m = std::stoull(h.substr(mp + 3));
    t = std::stoull(h.substr(tp + 3));
    return true;
}
}  // namespace

std::string algorithm(const std::string &h) {
    if (h.rfind("$argon2id$", 0) == 0) return "argon2id";
    if (h.rfind("$argon2i$", 0) == 0) return "argon2i";
    if (h.rfind("$2a$", 0) == 0 || h.rfind("$2b$", 0) == 0 || h.rfind("$2y$", 0) == 0) return "bcrypt";
    return "";
}

std::string make(const std::string &password, const Argon2Options &o) {
    initSodium();
    char out[crypto_pwhash_STRBYTES];
    if (crypto_pwhash_str_alg(out, password.data(), password.size(), o.opsLimit, o.memLimitKiB * 1024,
                              crypto_pwhash_ALG_ARGON2ID13) != 0)
        throw std::runtime_error("Express++ hash: out of memory while hashing");
    return out;
}

std::string bcrypt(const std::string &password, int rounds) {
    if (rounds < 4 || rounds > 31) throw std::invalid_argument("Express++ hash: bcrypt rounds must be 4..31");
    char setting[CRYPT_GENSALT_OUTPUT_SIZE];
    if (!crypt_gensalt_rn("$2b$", static_cast<unsigned long>(rounds), nullptr, 0, setting, sizeof setting))
        throw std::runtime_error("Express++ hash: bcrypt is not available in this system's libcrypt");
    auto data = std::make_unique<crypt_data>();
    std::memset(data.get(), 0, sizeof(crypt_data));
    const char *h = crypt_r(password.c_str(), setting, data.get());
    if (!h || h[0] == '*') throw std::runtime_error("Express++ hash: bcrypt failed");
    return h;
}

bool verify(const std::string &password, const std::string &h) {
    std::string alg = algorithm(h);
    if (alg == "argon2id" || alg == "argon2i") {
        initSodium();
        return crypto_pwhash_str_verify(h.c_str(), password.data(), password.size()) == 0;
    }
    if (alg == "bcrypt") {
        std::string setting = h;
        if (setting.rfind("$2y$", 0) == 0) setting[2] = 'b';  // PHP's $2y$ is the same as $2b$
        auto data = std::make_unique<crypt_data>();
        std::memset(data.get(), 0, sizeof(crypt_data));
        const char *out = crypt_r(password.c_str(), setting.c_str(), data.get());
        if (!out || out[0] == '*') return false;
        size_t n = std::strlen(out);
        return n == setting.size() && sodium_memcmp(out, setting.data(), n) == 0;
    }
    return false;
}

bool needsRehash(const std::string &h, const Argon2Options &o) {
    if (algorithm(h) != "argon2id") return true;
    unsigned long long m = 0, t = 0;
    if (!argonParams(h, m, t)) return true;
    return m < o.memLimitKiB || t < o.opsLimit;
}

}  // namespace express::hash
