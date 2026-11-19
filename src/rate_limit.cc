#include "express/rate_limit.hpp"

#include <algorithm>

namespace express {

namespace {
long long nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}
}  // namespace

RateLimitStore::Hit MemoryStore::increment(const std::string &key, long long windowMs) {
    long long t = nowMs();
    std::lock_guard<std::mutex> lk(m_);
    if (t - lastSweepMs_ > windowMs && hits_.size() > 1024) {  // drop finished windows now and then
        for (auto it = hits_.begin(); it != hits_.end();)
            it = it->second.resetAtMs <= t ? hits_.erase(it) : std::next(it);
        lastSweepMs_ = t;
    }
    Hit &h = hits_[key];
    if (h.resetAtMs <= t) {
        h.count = 0;
        h.resetAtMs = t + windowMs;
    }
    h.count++;
    return h;
}

void MemoryStore::decrement(const std::string &key) {
    std::lock_guard<std::mutex> lk(m_);
    auto it = hits_.find(key);
    if (it != hits_.end() && it->second.count > 0) it->second.count--;
}

void MemoryStore::reset(const std::string &key) {
    std::lock_guard<std::mutex> lk(m_);
    hits_.erase(key);
}

RateLimiter rateLimit(RateLimitOptions o) {
    if (!o.store) o.store = std::make_shared<MemoryStore>();
    if (o.windowMs <= 0 || o.limit <= 0) throw std::invalid_argument("Express++ rateLimit: windowMs and limit must be > 0");
    auto store = o.store;
    Middleware mw = [o](Req req, Res res, Next next) {
        if (o.skip && o.skip(req)) return next();
        std::string key = o.keyGenerator ? o.keyGenerator(req) : req.ip();
        auto hit = o.store->increment(key, o.windowMs);
        long long remaining = std::max(0LL, o.limit - hit.count);
        long long resetSec = std::max(0LL, (hit.resetAtMs - nowMs() + 999) / 1000);
        if (o.standardHeaders) {
            res.set("RateLimit-Policy", std::to_string(o.limit) + ";w=" + std::to_string(o.windowMs / 1000));
            res.set("RateLimit-Limit", std::to_string(o.limit));
            res.set("RateLimit-Remaining", std::to_string(remaining));
            res.set("RateLimit-Reset", std::to_string(resetSec));
        }
        if (o.legacyHeaders) {
            res.set("X-RateLimit-Limit", std::to_string(o.limit));
            res.set("X-RateLimit-Remaining", std::to_string(remaining));
            res.set("X-RateLimit-Reset", std::to_string((hit.resetAtMs + 999) / 1000));
        }
        if (hit.count > o.limit) {
            res.set("Retry-After", std::to_string(resetSec));
            if (o.handler) return o.handler(req, res);
            throw HttpError(o.statusCode, o.message);
        }
        if (o.skipSuccessfulRequests || o.skipFailedRequests) {
            auto st = o.store;
            bool skipOk = o.skipSuccessfulRequests, skipFail = o.skipFailedRequests;
            res.onFinish([st, key, skipOk, skipFail](const FinishInfo &i) {
                bool failed = i.status >= 400;
                if ((failed && skipFail) || (!failed && skipOk)) st->decrement(key);
            });
        }
        next();
    };
    return RateLimiter{std::move(mw), store};
}

}  // namespace express
