#pragma once

#include <stddef.h>
#include <stdint.h>

namespace sentinel {
namespace realtime {

struct MotionVector {
  double x = 0;
  double y = 0;
  double z = 0;
};

// Latest motion of the authenticated iPhone, fed by Protocol v1
// motion.sample frames (linear acceleration in m/s², device frame).
class PhoneMotion {
 public:
  static constexpr uint32_t kActiveMs = 2000;
  static constexpr double kFullScaleMs2 = 8.0;  // intensity 1.0

  void ingest(const char* deviceId, const MotionVector& linear,
              uint32_t nowMs);

  bool active(uint32_t nowMs) const;
  double magnitude() const { return magnitude_; }
  double peak() const { return peak_; }
  // Smoothed |linear| / kFullScaleMs2 in 0..1; 0 when inactive.
  float intensity(uint32_t nowMs) const;
  uint32_t samples() const { return samples_; }
  bool hasSample() const { return samples_ > 0; }
  uint32_t ageMs(uint32_t nowMs) const { return nowMs - lastMs_; }
  const char* deviceId() const { return deviceId_; }

 private:
  char deviceId_[65]{};
  double magnitude_ = 0;
  double smoothed_ = 0;
  double peak_ = 0;
  uint32_t samples_ = 0;
  uint32_t lastMs_ = 0;
};

}  // namespace realtime
}  // namespace sentinel
