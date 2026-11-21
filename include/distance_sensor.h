#pragma once

#include <Arduino.h>

#include "config.h"

namespace sentinel {

// Non-blocking HC-SR04 driver with timeout and median filtering.
class DistanceSensor {
 public:
  enum class MeasurementStatus : uint8_t {
    Idle,
    Valid,
    NoEcho,
    OutOfRange
  };

  bool begin(uint8_t trigPin, uint8_t echoPin);
  void update();

  bool hasSample() const { return sampleAvailable_; }
  void clearSample() { sampleAvailable_ = false; }

  float distanceCm() const { return filtered_; }
  uint32_t lastDurationUs() const { return lastDurationUs_; }
  MeasurementStatus status() const { return status_; }
  bool lastValid() const { return valid_; }
  uint32_t validCount() const { return validCount_; }
  uint32_t errorCount() const { return errorCount_; }

 private:
  static void IRAM_ATTR onEchoChange();

  void startPulse();
  void processSample();

  static DistanceSensor* instance_;

  uint8_t trigPin_ = 0;
  uint8_t echoPin_ = 0;

  volatile uint32_t riseUs_ = 0;
  volatile uint32_t fallUs_ = 0;
  volatile bool waitingRise_ = false;
  volatile bool measuring_ = false;
  volatile bool sampleReady_ = false;
  volatile uint32_t pulseStartUs_ = 0;

  unsigned long lastTriggerMs_ = 0;

  float samples_[config::kSampleCount]{};
  size_t sampleIndex_ = 0;
  size_t sampleCount_ = 0;

  float filtered_ = 0.0f;
  uint32_t lastDurationUs_ = 0;
  MeasurementStatus status_ = MeasurementStatus::Idle;
  bool valid_ = false;
  bool sampleAvailable_ = false;
  bool timedOut_ = false;

  uint32_t validCount_ = 0;
  uint32_t errorCount_ = 0;
};

}  // namespace sentinel
