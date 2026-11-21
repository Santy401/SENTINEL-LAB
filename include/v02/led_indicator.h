#pragma once

#include "v02/motion_engine.h"

#include <stdint.h>

namespace sentinel {
namespace v02 {

enum class LedId : uint8_t {
  Stable = 0,
  Movement = 1,
  Alert = 2
};

// Virtual-only indicator layer. No pin is assigned and no GPIO is written:
// physical LEDs require verified current-limiting hardware first.
class LedIndicator {
 public:
  void begin();
  void update(MotionState state);

  bool isOn(LedId id) const;
  static const char* ledName(LedId id);
  static const char* mode() { return "VIRTUAL"; }

 private:
  bool stable_ = false;
  bool movement_ = false;
  bool alert_ = false;
};

}  // namespace v02
}  // namespace sentinel
