#pragma once
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <future>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace lilia::engine {

class ThreadPool {
 public:
  static ThreadPool& instance(int desired_threads = -1) {
    static ThreadPool pool(desired_threads);
    return pool;
  }

  // Submit a job returning T
  template <class F, class... Args>
  auto submit(F&& f, Args&&... args) -> std::future<std::invoke_result_t<F, Args...>> {
    using R = std::invoke_result_t<F, Args...>;

    // packaged_task in shared_ptr, so that Callable is copyable
    auto task_ptr = std::make_shared<std::packaged_task<R()>>(
        std::bind(std::forward<F>(f), std::forward<Args>(args)...));

    std::future<R> fut = task_ptr->get_future();

    {
      std::lock_guard<std::mutex> lk(m_);
      q_.emplace([task_ptr]() mutable {
        // shared_ptr keeps the task alive
        (*task_ptr)();
      });
    }
    cv_.notify_one();
    return fut;
  }

  void maybe_resize(int desired) {
    if (desired <= 0) return;
    std::lock_guard<std::mutex> lk(m_);
    // simple policy: never shrink; grow up to desired once
    while (!stop_ && threads_.size() < (size_t)desired) {
      threads_.emplace_back([this] { worker(); });
    }
  }

  ~ThreadPool() {
    {
      std::lock_guard<std::mutex> lk(m_);
      stop_ = true;
    }
    cv_.notify_all();
    for (auto& t : threads_)
      if (t.joinable()) t.join();
  }

 private:
  explicit ThreadPool(int desired_threads) {
    int n = desired_threads > 0 ? desired_threads : (int)std::thread::hardware_concurrency();
    if (n <= 0) n = 1;
    threads_.reserve(n);
    for (int i = 0; i < n; ++i) threads_.emplace_back([this] { worker(); });
  }

  void worker() {
    for (;;) {
      std::function<void()> job;
      {
        std::unique_lock<std::mutex> lk(m_);
        cv_.wait(lk, [&] { return stop_ || !q_.empty(); });
        if (stop_ && q_.empty()) return;
        job = std::move(q_.front());
        q_.pop();
      }
      job();
    }
  }

  std::mutex m_;
  std::condition_variable cv_;
  std::queue<std::function<void()>> q_;
  std::vector<std::thread> threads_;
  bool stop_ = false;
};

}  // namespace lilia::engine
