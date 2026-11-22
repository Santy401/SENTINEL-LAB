#include "realtime/phone_motion.h"

#include <math.h>
#include <stdio.h>

namespace sentinel {
namespace realtime {

void PhoneMotion::ingest(const char* deviceId, const MotionVector& linear,
                         uint32_t nowMs) {
  const double m =
      sqrt(linear.x * linear.x + linear.y * linear.y + linear.z * linear.z);
  if (!isfinite(m)) return;
  if (deviceId) snprintf(deviceId_, sizeof(deviceId_), "%s", deviceId);
  magnitude_ = m;
  // Samples arrive at ~5 Hz: rise fast, fall at a readable pace.
  smoothed_ = m > smoothed_ ? 0.6 * m + 0.4 * smoothed_
                            : 0.35 * m + 0.65 * smoothed_;
  if (m > peak_) peak_ = m;
  ++samples_;
  lastMs_ = nowMs;
}

bool PhoneMotion::active(uint32_t nowMs) const {
  return samples_ > 0 && uint32_t(nowMs - lastMs_) < kActiveMs;
}

float PhoneMotion::intensity(uint32_t nowMs) const {
  if (!active(nowMs)) return 0.0f;
  const double v = smoothed_ / kFullScaleMs2;
  return static_cast<float>(v < 0 ? 0 : (v > 1 ? 1 : v));
}

}  // namespace realtime
}  // namespace sentinel
