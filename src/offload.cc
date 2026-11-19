#include "express/offload.hpp"

#include <algorithm>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

namespace express::detail {

namespace {
class Pool {
  public:
    Pool() {
        unsigned n = std::max(2u, std::thread::hardware_concurrency());
        for (unsigned i = 0; i < n; i++) workers_.emplace_back([this] { loop(); });
    }
    ~Pool() {
        {
            std::lock_guard<std::mutex> lk(m_);
            stop_ = true;
        }
        cv_.notify_all();
        for (auto &t : workers_) t.join();
    }
    void post(std::function<void()> job) {
        {
            std::lock_guard<std::mutex> lk(m_);
            q_.push_back(std::move(job));
        }
        cv_.notify_one();
    }

  private:
    void loop() {
        for (;;) {
            std::function<void()> job;
            {
                std::unique_lock<std::mutex> lk(m_);
                cv_.wait(lk, [this] { return stop_ || !q_.empty(); });
                if (stop_ && q_.empty()) return;
                job = std::move(q_.front());
                q_.pop_front();
            }
            job();
        }
    }
    std::mutex m_;
    std::condition_variable cv_;
    std::deque<std::function<void()>> q_;
    std::vector<std::thread> workers_;
    bool stop_ = false;
};
}  // namespace

void runOnPool(std::function<void()> job) {
    static Pool pool;
    pool.post(std::move(job));
}

}  // namespace express::detail
