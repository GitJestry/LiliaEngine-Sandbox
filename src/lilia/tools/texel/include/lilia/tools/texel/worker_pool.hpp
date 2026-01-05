#pragma once
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

namespace lilia::tools::texel {

// Fixed-size worker pool optimized for "run same callback on each thread" workloads.
class WorkerPool {
 public:
  explicit WorkerPool(int n) : n_(n < 1 ? 1 : n) {
    threads_.reserve(n_);
    for (int i = 0; i < n_; ++i) threads_.emplace_back([this, i] { worker_loop(i); });
  }

  WorkerPool(const WorkerPool&) = delete;
  WorkerPool& operator=(const WorkerPool&) = delete;

  ~WorkerPool() {
    {
      std::lock_guard<std::mutex> lk(mu_);
      stop_ = true;
      ++ticket_;
      task_ = {};
    }
    cv_.notify_all();
    for (auto& t : threads_) {
      if (t.joinable()) t.join();
    }
  }

  int size() const noexcept { return n_; }

  void run(const std::function<void(int)>& fn) {
    uint64_t myTicket;
    {
      std::lock_guard<std::mutex> lk(mu_);
      task_ = fn;
      doneCount_ = 0;
      myTicket = ++ticket_;
    }
    cv_.notify_all();

    std::unique_lock<std::mutex> lk(mu_);
    doneCv_.wait(lk, [&] { return doneTicket_ == myTicket && doneCount_ == n_; });
  }

 private:
  const int n_;
  std::vector<std::thread> threads_;

  std::mutex mu_;
  std::condition_variable cv_;
  std::condition_variable doneCv_;

  std::function<void(int)> task_;
  uint64_t ticket_{0};
  uint64_t doneTicket_{0};
  int doneCount_{0};
  bool stop_{false};

  void worker_loop(int id) {
    uint64_t seen = 0;
    for (;;) {
      std::function<void(int)> local;
      uint64_t myTicket = 0;

      {
        std::unique_lock<std::mutex> lk(mu_);
        cv_.wait(lk, [&] { return stop_ || ticket_ != seen; });
        if (stop_) return;
        seen = ticket_;
        myTicket = ticket_;
        local = task_;
      }

      if (local) local(id);

      {
        std::lock_guard<std::mutex> lk(mu_);
        if (doneTicket_ != myTicket) {
          doneTicket_ = myTicket;
          doneCount_ = 0;
        }
        if (++doneCount_ == n_) doneCv_.notify_one();
      }
    }
  }
};

}  // namespace lilia::tools::texel
