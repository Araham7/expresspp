#pragma once
/* express::offload(fn): run slow, blocking work (password hashing, image resizing, a blocking library call)
   on Express++'s worker pool and co_await the result. The IO loop keeps serving other requests meanwhile,
   and the coroutine continues on the same IO loop it started on.

     app.post("/x", [](express::Req req, express::Res res) -> express::Task<> {
         std::string out = co_await express::offload([] { return slowThing(); });
         res.send(out);
     });

   Outside a coroutine: express::offload(fn).get() runs it right here.                              */

#include <trantor/net/EventLoop.h>

#include <coroutine>
#include <exception>
#include <functional>
#include <optional>
#include <type_traits>
#include <utility>

namespace express {

namespace detail {
void runOnPool(std::function<void()> job);  // Express++ worker pool (max(2, cores) threads)
}

template <class T>
class [[nodiscard]] Offload {
  public:
    explicit Offload(std::function<T()> fn) : fn_(std::move(fn)) {}
    bool await_ready() const noexcept { return false; }
    void await_suspend(std::coroutine_handle<> h) {
        trantor::EventLoop *loop = trantor::EventLoop::getEventLoopOfCurrentThread();
        detail::runOnPool([this, h, loop] {
            try {
                if constexpr (std::is_void_v<T>)
                    fn_();
                else
                    value_.emplace(fn_());
            } catch (...) {
                err_ = std::current_exception();
            }
            if (loop)
                loop->queueInLoop([h] { h.resume(); });
            else
                h.resume();
        });
    }
    T await_resume() {
        if (err_) std::rethrow_exception(err_);
        if constexpr (!std::is_void_v<T>) return std::move(*value_);
    }
    T get() { return fn_(); }

  private:
    std::function<T()> fn_;
    std::conditional_t<std::is_void_v<T>, char, std::optional<T>> value_{};
    std::exception_ptr err_;
};

template <class F>
auto offload(F &&fn) {
    using T = std::invoke_result_t<F &>;
    return Offload<T>(std::function<T()>(std::forward<F>(fn)));
}

}  // namespace express
