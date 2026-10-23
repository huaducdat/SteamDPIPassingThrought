#pragma once

#include <condition_variable>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <utility>

namespace steamdpi {

template <typename T> class BoundedEventQueue {
public:
  explicit BoundedEventQueue(std::size_t capacity) : capacity_(capacity) {}

  bool push(T value) {
    std::lock_guard lock(mutex_);
    if (closed_ || queue_.size() >= capacity_) return false;
    queue_.push_back(std::move(value));
    ++pushes_;
    if(queue_.size()>highWaterMark_)highWaterMark_=queue_.size();
    available_.notify_one();
    return true;
  }

  std::optional<T> waitPop() {
    std::unique_lock lock(mutex_);
    available_.wait(lock, [&] { return closed_ || !queue_.empty(); });
    if (queue_.empty()) return std::nullopt;
    T value = std::move(queue_.front());
    queue_.pop_front();
    ++pops_;
    return value;
  }

  template <typename Rep, typename Period>
  std::optional<T> waitPopFor(const std::chrono::duration<Rep, Period>& timeout) {
    std::unique_lock lock(mutex_);
    available_.wait_for(lock, timeout, [&] { return closed_ || !queue_.empty(); });
    if (queue_.empty()) return std::nullopt;
    T value = std::move(queue_.front());
    queue_.pop_front();
    ++pops_;
    return value;
  }

  void close() {
    std::lock_guard lock(mutex_);
    closed_ = true;
    available_.notify_all();
  }

  std::size_t size() const {
    std::lock_guard lock(mutex_);
    return queue_.size();
  }

  std::size_t highWaterMark() const { std::lock_guard lock(mutex_); return highWaterMark_; }
  std::uint64_t pushes() const { std::lock_guard lock(mutex_); return pushes_; }
  std::uint64_t pops() const { std::lock_guard lock(mutex_); return pops_; }

  void clear() {
    std::lock_guard lock(mutex_);
    queue_.clear();
    highWaterMark_=0; pushes_=0; pops_=0;
  }

private:
  const std::size_t capacity_;
  mutable std::mutex mutex_;
  std::condition_variable available_;
  std::deque<T> queue_;
  bool closed_ = false;
  std::size_t highWaterMark_ = 0;
  std::uint64_t pushes_ = 0, pops_ = 0;
};

} // namespace steamdpi
