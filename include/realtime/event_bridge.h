#pragma once

#include <stdint.h>

namespace sentinel {
namespace realtime {

// Tracks the monotonically increasing EventEngine sequence so a reconnecting
// WebSocket client does not receive the whole historical ring, while events
// produced while no client was connected are still skipped correctly.
class EventBridge {
 public:
  void begin(uint32_t highestExistingSequence) {
    lastSequence_ = highestExistingSequence;
  }

  bool consume(uint32_t sequence, bool simulated) {
    if (sequence == 0 || simulated) return false;
    const bool newer = sequence > lastSequence_ ||
                       (lastSequence_ - sequence) > 0x7FFFFFFFULL;
    if (!newer) return false;
    lastSequence_ = sequence;
    return true;
  }

  uint32_t lastSequence() const { return lastSequence_; }

 private:
  uint32_t lastSequence_ = 0;
};

}  // namespace realtime
}  // namespace sentinel
