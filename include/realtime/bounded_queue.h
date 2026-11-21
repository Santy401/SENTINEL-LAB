#pragma once

#include <stddef.h>
#include <stdint.h>

namespace sentinel {
namespace realtime {

// Fixed-capacity byte-message queue. Overflow drops the newest message and is
// counted so operators can see backpressure instead of exhausting heap.
template <size_t Capacity, size_t MessageSize>
class BoundedQueue {
 public:
  bool push(const char* data, size_t length) {
    if (!data || length == 0 || length > MessageSize || count_ == Capacity) {
      if (count_ == Capacity) ++dropped_;
      return false;
    }
    const size_t index = (head_ + count_) % Capacity;
    for (size_t i = 0; i < length; ++i) data_[index][i] = data[i];
    lengths_[index] = static_cast<uint16_t>(length);
    ++count_;
    return true;
  }

  bool pop(char* output, size_t outputCapacity, size_t& length) {
    length = 0;
    if (!output || count_ == 0) return false;
    if (outputCapacity < lengths_[head_]) return false;
    length = lengths_[head_];
    for (size_t i = 0; i < length; ++i) output[i] = data_[head_][i];
    head_ = (head_ + 1) % Capacity;
    --count_;
    return true;
  }

  size_t size() const { return count_; }
  bool empty() const { return count_ == 0; }
  uint32_t dropped() const { return dropped_; }
  void clear() {
    head_ = 0;
    count_ = 0;
  }

 private:
  char data_[Capacity][MessageSize]{};
  uint16_t lengths_[Capacity]{};
  size_t head_ = 0;
  size_t count_ = 0;
  uint32_t dropped_ = 0;
};

}  // namespace realtime
}  // namespace sentinel
