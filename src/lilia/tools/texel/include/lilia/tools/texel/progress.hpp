#pragma once
#include <atomic>
#include <chrono>
#include <cstddef>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <utility>

namespace lilia::tools::texel {

// Lightweight progress meter. Thread-safe when constructed with threadSafe=true.
class ProgressMeter {
 public:
  ProgressMeter(std::string label, std::size_t total, int intervalMs = 750, bool threadSafe = false)
      : label_(std::move(label)),
        total_(total),
        intervalMs_(intervalMs),
        threadSafe_(threadSafe),
        start_(std::chrono::steady_clock::now()),
        last_(start_) {}

  void add(std::size_t delta = 1) {
    if (finished_.load(std::memory_order_acquire)) return;
    current_.fetch_add(delta, std::memory_order_relaxed);
    tick(false);
  }

  void update(std::size_t value) {
    if (finished_.load(std::memory_order_acquire)) return;
    current_.store(value, std::memory_order_relaxed);
    tick(false);
  }

  void set_status(std::string s, bool flush = false) {
    if (finished_.load(std::memory_order_acquire)) return;
    if (threadSafe_) {
      std::lock_guard<std::mutex> lk(mu_);
      status_ = std::move(s);
    } else {
      status_ = std::move(s);
    }
    if (flush) tick(true);
  }

  void finish() {
    if (finished_.exchange(true, std::memory_order_acq_rel)) return;
    current_.store(total_, std::memory_order_relaxed);
    tick(true);
    if (threadSafe_) {
      std::lock_guard<std::mutex> lk(mu_);
      std::cout << "\n";
    } else {
      std::cout << "\n";
    }
  }

 private:
  std::string label_;
  std::size_t total_{0};
  std::atomic<std::size_t> current_{0};
  int intervalMs_{750};
  bool threadSafe_{false};

  std::chrono::steady_clock::time_point start_;
  std::chrono::steady_clock::time_point last_;
  std::atomic<bool> finished_{false};

  std::mutex mu_;
  std::string status_;

  static std::string fmt_hms(std::chrono::seconds s) {
    long t = s.count();
    int h = static_cast<int>(t / 3600);
    int m = static_cast<int>((t % 3600) / 60);
    int sec = static_cast<int>(t % 60);
    std::ostringstream os;
    if (h > 0)
      os << h << ":" << std::setw(2) << std::setfill('0') << m << ":" << std::setw(2) << sec;
    else
      os << m << ":" << std::setw(2) << std::setfill('0') << sec;
    return os.str();
  }

  void tick(bool force) {
    if (!force && finished_.load(std::memory_order_acquire)) return;

    std::unique_lock<std::mutex> lk(mu_, std::defer_lock);
    if (threadSafe_) lk.lock();

    const auto now = std::chrono::steady_clock::now();
    const auto sinceMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_).count();

    std::size_t cur = current_.load(std::memory_order_relaxed);
    if (cur > total_) cur = total_;

    if (!force && sinceMs < intervalMs_ && cur != total_) return;
    last_ = now;

    const double pct = total_ ? (100.0 * double(cur) / double(total_)) : 0.0;
    const double elapsedSec = std::chrono::duration<double>(now - start_).count();
    const double rate = elapsedSec > 0.0 ? double(cur) / elapsedSec : 0.0;
    const double remainSec = (rate > 0.0 && total_ >= cur) ? double(total_ - cur) / rate : 0.0;

    const auto eta = std::chrono::seconds(static_cast<long long>(remainSec + 0.5));
    const auto elapsed = std::chrono::seconds(static_cast<long long>(elapsedSec + 0.5));

    std::ostringstream line;
    line << "\r" << label_ << " " << std::fixed << std::setprecision(1) << pct << "% "
         << "(" << cur << "/" << total_ << ")  "
         << "elapsed " << fmt_hms(elapsed) << "  ETA ~" << fmt_hms(eta);
    if (rate > 0.0) line << "  rate " << std::setprecision(1) << rate << "/s";
    if (!status_.empty()) line << "  " << status_;
    std::cout << line.str() << std::flush;
  }
};

}  // namespace lilia::tools::texel
