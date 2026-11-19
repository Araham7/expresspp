#pragma once
/* express::rateLimit: express-rate-limit for Express++.

     app.use(express::rateLimit({.windowMs = 15 * 60 * 1000, .limit = 100}));        // 100 req / 15 min per IP
     app.post("/login", express::rateLimit({.windowMs = 60000, .limit = 5,
                                            .message = "Too many login attempts, try again later"}), login);

   - over the limit: 429 {"error": message} + Retry-After
   - headers: RateLimit-Limit / -Remaining / -Reset / RateLimit-Policy (standardHeaders, on by default)
              and X-RateLimit-* (legacyHeaders, off by default)
   - key: req.ip() by default (turn on app.set("trust proxy", true) behind Nginx!), or your keyGenerator
   - skipSuccessfulRequests / skipFailedRequests: only count failures / successes (login brute force)
   - store: in-memory by default (one server process). Implement RateLimitStore for Redis (shared).      */

#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include "app.hpp"

namespace express {

class RateLimitStore {
  public:
    struct Hit {
        long long count = 0;
        long long resetAtMs = 0;  // unix ms when the window ends
    };
    virtual ~RateLimitStore() = default;
    virtual Hit increment(const std::string &key, long long windowMs) = 0;
    virtual void decrement(const std::string &key) = 0;
    virtual void reset(const std::string &key) = 0;
};

// fixed window, in memory, cleans itself up
class MemoryStore : public RateLimitStore {
  public:
    Hit increment(const std::string &key, long long windowMs) override;
    void decrement(const std::string &key) override;
    void reset(const std::string &key) override;

  private:
    std::mutex m_;
    std::unordered_map<std::string, Hit> hits_;
    long long lastSweepMs_ = 0;
};

struct RateLimitOptions {
    long long windowMs = 60 * 1000;
    long long limit = 60;  // "max" in older express-rate-limit
    std::string message = "Too many requests, please try again later.";
    int statusCode = 429;
    bool standardHeaders = true;
    bool legacyHeaders = false;
    bool skipSuccessfulRequests = false;  // status < 400 not counted
    bool skipFailedRequests = false;      // status >= 400 not counted
    std::function<std::string(const Req &)> keyGenerator;  // default: req.ip()
    std::function<bool(const Req &)> skip;                 // true = don't limit this request
    std::function<void(Req, Res)> handler;                 // your own 429 response
    std::shared_ptr<RateLimitStore> store;                 // default: a new MemoryStore
};

// The middleware also gives access to its store: auto rl = express::rateLimit(...); rl.store->reset(ip);
struct RateLimiter {
    Middleware middleware;
    std::shared_ptr<RateLimitStore> store;
    operator Middleware() const { return middleware; }
    void operator()(Req req, Res res, Next next) const { middleware(req, res, next); }
};

RateLimiter rateLimit(RateLimitOptions o = {});

}  // namespace express
