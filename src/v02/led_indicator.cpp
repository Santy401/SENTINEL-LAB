#include "v02/led_indicator.h"

namespace sentinel {
namespace v02 {

void LedIndicator::begin() {
  stable_ = false;
  movement_ = false;
  alert_ = false;
}

void LedIndicator::update(MotionState state) {
  stable_ = (state == MotionState::Stable);
  movement_ = (state == MotionState::Movement);
  alert_ = (state == MotionState::Alert);
  // SensorOffline intentionally turns all three indicators off.
}

bool LedIndicator::isOn(LedId id) const {
  switch (id) {
    case LedId::Stable:   return stable_;
    case LedId::Movement: return movement_;
    case LedId::Alert:    return alert_;
    default:              return false;
  }
}

const char* LedIndicator::ledName(LedId id) {
  switch (id) {
    case LedId::Stable:   return "LED 1 (stable)";
    case LedId::Movement: return "LED 2 (movement)";
    case LedId::Alert:    return "LED 3 (alert)";
    default:              return "unknown";
  }
}

}  // namespace v02
}  // namespace sentinel
