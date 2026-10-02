#pragma once
#include <array>
#include <atomic>
namespace qw {
// One producer (VST processing thread), one playback worker. Full queue is
// observable.
template <class T, unsigned N> class Ring {
  std::array<T, N> data_{};
  std::atomic<unsigned> read_{0}, write_{0};

public:
  static_assert(std::atomic<unsigned>::is_always_lock_free,
                "Need lock-free indices");
  bool push(const T &v) {
    auto w = write_.load(std::memory_order_relaxed), next = (w + 1) % N;
    if (next == read_.load(std::memory_order_acquire))
      return false;
    data_[w] = v;
    write_.store(next, std::memory_order_release);
    return true;
  }
  bool pop(T &v) {
    auto r = read_.load(std::memory_order_relaxed);
    if (r == write_.load(std::memory_order_acquire))
      return false;
    v = data_[r];
    read_.store((r + 1) % N, std::memory_order_release);
    return true;
  }
};
} // namespace qw
