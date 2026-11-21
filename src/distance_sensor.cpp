#include "distance_sensor.h"

#include "distance_math.h"

namespace sentinel {

DistanceSensor* DistanceSensor::instance_ = nullptr;

bool DistanceSensor::begin(uint8_t trigPin, uint8_t echoPin) {
  trigPin_ = trigPin;
  echoPin_ = echoPin;

  pinMode(trigPin_, OUTPUT);
  digitalWrite(trigPin_, LOW);
  pinMode(echoPin_, INPUT);

  instance_ = this;
  attachInterrupt(digitalPinToInterrupt(echoPin_), onEchoChange, CHANGE);
  return true;
}

void IRAM_ATTR DistanceSensor::onEchoChange() {
  DistanceSensor* self = instance_;
  if (self == nullptr || !self->measuring_) {
    return;
  }

  const uint32_t now = micros();

  if (self->waitingRise_) {
    if (digitalRead(self->echoPin_) == HIGH) {
      self->riseUs_ = now;
      self->waitingRise_ = false;
    }
    return;
  }

  if (digitalRead(self->echoPin_) == LOW) {
    self->fallUs_ = now;
    self->measuring_ = false;
    self->sampleReady_ = true;
  }
}

void DistanceSensor::startPulse() {
  // Arm capture state BEFORE driving TRIG: the echo ISR may fire immediately
  // after the 10 us pulse, so measuring_/waitingRise_ must already be valid.
  riseUs_ = 0;
  fallUs_ = 0;
  waitingRise_ = true;
  measuring_ = true;
  timedOut_ = false;
  pulseStartUs_ = micros();

  digitalWrite(trigPin_, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin_, LOW);
}

void DistanceSensor::processSample() {
  lastDurationUs_ = 0;

  if (timedOut_ || riseUs_ == 0 || fallUs_ <= riseUs_) {
    status_ = MeasurementStatus::NoEcho;
    valid_ = false;
    ++errorCount_;
    return;
  }

  const uint32_t durationUs = fallUs_ - riseUs_;
  lastDurationUs_ = durationUs;
  const float cm = math::distanceCm(durationUs, config::kSpeedOfSoundCmPerUs);

  if (cm < config::kMinValidDistanceCm || cm > config::kMaxValidDistanceCm) {
    status_ = MeasurementStatus::OutOfRange;
    valid_ = false;
    ++errorCount_;
    return;
  }

  samples_[sampleIndex_] = cm;
  sampleIndex_ = (sampleIndex_ + 1) % config::kSampleCount;
  if (sampleCount_ < config::kSampleCount) {
    ++sampleCount_;
  }

  float ordered[config::kSampleCount];
  for (size_t i = 0; i < sampleCount_; ++i) {
    ordered[i] = samples_[i];
  }
  filtered_ = math::median(ordered, sampleCount_);

  status_ = MeasurementStatus::Valid;
  valid_ = true;
  ++validCount_;
}

void DistanceSensor::update() {
  if (measuring_ && (micros() - pulseStartUs_ > config::kEchoTimeoutUs)) {
    measuring_ = false;
    waitingRise_ = false;
    fallUs_ = 0;
    timedOut_ = true;
    sampleReady_ = true;
  }

  if (sampleReady_) {
    processSample();
    sampleReady_ = false;
    sampleAvailable_ = true;
  }

  if (!measuring_ && (millis() - lastTriggerMs_ >= config::kSensorIntervalMs)) {
    lastTriggerMs_ = millis();
    startPulse();
  }
}

}  // namespace sentinel
