#pragma once

#include <stddef.h>
#include <stdint.h>

namespace sentinel {
namespace realtime {

// Visual states of the on-board WS2812 RGB LED (ESP32-S3-DevKitC-1, GPIO48).
enum class RgbMode : uint8_t { Boot, Offline, Client, Synced, Motion, Event };

const char* rgbModeName(RgbMode mode);

struct RgbColor {
  uint8_t r = 0;
  uint8_t g = 0;
  uint8_t b = 0;
};

struct RgbInputs {
  uint32_t nowMs = 0;
  size_t clients = 0;
  bool synced = false;
  bool motionActive = false;
  float intensity = 0.0f;  // phone shake, 0..1
};

// Pure colour logic, host-testable. Transitions are exponentially smoothed
// and every animation is slower than 1.5 Hz: no strobe or rapid flashing.
// Output channels never exceed kMaxLevel to keep the LED comfortable to look
// at and well inside the board's power budget.
class RgbEngine {
 public:
  static constexpr uint8_t kMaxLevel = 64;
  static constexpr uint32_t kBootMs = 1200;
  static constexpr uint32_t kEventMs = 700;
  static constexpr uint32_t kBreathPeriodMs = 4000;

  void begin(uint32_t nowMs);
  void pulseEvent(uint32_t nowMs);
  // Advances the animation; returns true when the output colour changed.
  bool update(const RgbInputs& inputs);

  RgbColor color() const { return output_; }
  RgbMode mode() const { return mode_; }

  // Target colour (0..1 per channel, before smoothing and gamma) for a mode.
  static void target(const RgbInputs& inputs, uint32_t bootMs,
                     RgbMode& mode, float out[3]);

 private:
  float current_[3]{};
  RgbColor output_{};
  RgbMode mode_ = RgbMode::Boot;
  uint32_t bootMs_ = 0;
  uint32_t lastMs_ = 0;
  uint32_t eventMs_ = 0;
  bool eventActive_ = false;
  bool started_ = false;
};

}  // namespace realtime
}  // namespace sentinel
