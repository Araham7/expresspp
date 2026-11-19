#pragma once
/* express::hash: password hashing (argon2 / bcrypt for Express++).

     std::string h = co_await express::hash::makeAsync(password);      // argon2id, on the worker pool
     bool ok       = co_await express::hash::verifyAsync(password, h);  // works for argon2 AND bcrypt hashes

     std::string h = express::hash::make(password);                     // blocking versions (scripts, tests)
     bool ok       = express::hash::verify(password, h);

   - Default is argon2id (libsodium), stored as the standard "$argon2id$v=19$m=...,t=...,p=1$salt$hash"
     string, the same format Node's `argon2` package writes and reads.
   - bcrypt ("$2a$" / "$2b$" / "$2y$") is supported too, so hashes from Node's `bcrypt` / `bcryptjs`
     keep working after a migration:  express::hash::bcrypt(password, 10)
   - needsRehash(h): true for bcrypt or weaker argon2 settings, so you can upgrade at login.
   Hashing takes ~50-100 ms on purpose: in a handler always use the *Async versions, never block an IO loop. */

#include <string>

#include "offload.hpp"

namespace express::hash {

struct Argon2Options {
    unsigned long long opsLimit = 3;           // iterations (t)
    size_t memLimitKiB = 64 * 1024;            // memory (m), 64 MiB
};

std::string make(const std::string &password, const Argon2Options &o = {});
bool verify(const std::string &password, const std::string &hash);  // argon2id/argon2i or bcrypt; false on junk
std::string bcrypt(const std::string &password, int rounds = 10);
bool needsRehash(const std::string &hash, const Argon2Options &o = {});
std::string algorithm(const std::string &hash);  // "argon2id" | "argon2i" | "bcrypt" | ""

inline Offload<std::string> makeAsync(std::string password, Argon2Options o = {}) {
    return Offload<std::string>([password = std::move(password), o] { return make(password, o); });
}
inline Offload<bool> verifyAsync(std::string password, std::string hash) {
    return Offload<bool>([password = std::move(password), hash = std::move(hash)] { return verify(password, hash); });
}
inline Offload<std::string> bcryptAsync(std::string password, int rounds = 10) {
    return Offload<std::string>([password = std::move(password), rounds] { return bcrypt(password, rounds); });
}

}  // namespace express::hash
