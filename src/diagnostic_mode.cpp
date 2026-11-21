#include "diagnostic_mode.h"

#if defined(SENTINEL_DIAGNOSTIC_MODE)

#include <Arduino.h>

namespace sentinel {
namespace diagnostic {

using MeasurementStatus = DistanceSensor::MeasurementStatus;

namespace {

struct Stats {
  uint32_t total = 0;
  uint32_t valid = 0;
  uint32_t noEcho = 0;
  uint32_t outOfRange = 0;
  float minCm = 0.0f;
  float maxCm = 0.0f;
  float sumCm = 0.0f;
};

Stats stats;

const char* statusName(MeasurementStatus status) {
  switch (status) {
    case MeasurementStatus::Valid:      return "VALID";
    case MeasurementStatus::NoEcho:     return "NO_ECHO";
    case MeasurementStatus::OutOfRange: return "OUT_OF_RANGE";
    default:                            return "IDLE";
  }
}

}  // namespace

void report(const DistanceSensor& sensor) {
  if (!sensor.hasSample()) {
    return;
  }

  ++stats.total;
  const MeasurementStatus status = sensor.status();

  if (status == MeasurementStatus::Valid) {
    ++stats.valid;
    const float cm = sensor.distanceCm();
    if ((stats.valid == 1) || (cm < stats.minCm)) {
      stats.minCm = cm;
    }
    if (cm > stats.maxCm) {
      stats.maxCm = cm;
    }
    stats.sumCm += cm;
  } else if (status == MeasurementStatus::NoEcho) {
    ++stats.noEcho;
  } else if (status == MeasurementStatus::OutOfRange) {
    ++stats.outOfRange;
  }

  const float avg = (stats.valid > 0) ? (stats.sumCm / stats.valid) : 0.0f;
  Serial.printf(
      "[DIAG] dur=%lu us dist=%.2f cm valid=%s status=%s "
      "total=%lu ok=%lu no_echo=%lu oor=%lu min=%.1f max=%.1f avg=%.1f\n",
      static_cast<unsigned long>(sensor.lastDurationUs()),
      sensor.distanceCm(),
      sensor.lastValid() ? "yes" : "no",
      statusName(status),
      static_cast<unsigned long>(stats.total),
      static_cast<unsigned long>(stats.valid),
      static_cast<unsigned long>(stats.noEcho),
      static_cast<unsigned long>(stats.outOfRange),
      stats.minCm,
      stats.maxCm,
      avg);
}

void printSummary() {
  const float avg = (stats.valid > 0) ? (stats.sumCm / stats.valid) : 0.0f;
  Serial.printf(
      "[DIAG-SUM] total=%lu ok=%lu no_echo=%lu oor=%lu min=%.1f max=%.1f avg=%.1f\n",
      static_cast<unsigned long>(stats.total),
      static_cast<unsigned long>(stats.valid),
      static_cast<unsigned long>(stats.noEcho),
      static_cast<unsigned long>(stats.outOfRange),
      stats.minCm, stats.maxCm, avg);
}

}  // namespace diagnostic
}  // namespace sentinel

#else

namespace sentinel {
namespace diagnostic {

void report(const DistanceSensor&) {}
void printSummary() {}

}  // namespace diagnostic
}  // namespace sentinel

#endif
