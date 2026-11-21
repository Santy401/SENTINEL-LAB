#pragma once
#include <stddef.h>
namespace sentinel { namespace rf {
template<class T, size_t N> class Ring {
 public:
  void push(const T& value) { data_[(start_ + size_) % N] = value;
    if (size_ < N) ++size_; else start_ = (start_ + 1) % N; }
  const T& at(size_t i) const { return data_[(start_ + i) % N]; }
  size_t size() const { return size_; }
  void clear() { start_ = size_ = 0; }
 private:
  static_assert(N > 0, "empty ring");
  T data_[N]{};
  size_t start_ = 0, size_ = 0;
};
} }
