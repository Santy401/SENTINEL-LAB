#include "servo_controller.h"

namespace sentinel {

bool ServoController::begin(uint8_t pin, int homeAngle, int protectAngle) {
  pin_ = pin;
  min_ = config::kServoMinAngle;
  max_ = config::kServoMaxAngle;

  home_ = constrain(homeAngle, min_, max_);
  protect_ = constrain(protectAngle, min_, max_);

  servo_.setPeriodHertz(config::kServoFrequencyHz);
  servo_.attach(pin_, config::kServoMinPulseUs, config::kServoMaxPulseUs);

  current_ = home_;
  target_ = home_;
  moving_ = false;
  servo_.write(current_);

  ready_ = true;
  return true;
}

void ServoController::setTarget(int angle) {
  if (!ready_) {
    return;
  }

  const int next = constrain(angle, min_, max_);
  if (next == target_) {
    return;
  }

  target_ = next;
  moveStartMs_ = millis();
  lastStepMs_ = 0;
  moving_ = (target_ != current_);
}

void ServoController::update() {
  if (!ready_ || !moving_) {
    return;
  }

  const unsigned long now = millis();

  if (now - moveStartMs_ > config::kServoMoveTimeoutMs) {
    servo_.write(target_);
    current_ = target_;
    moving_ = false;
    return;
  }

  if (now - lastStepMs_ < config::kServoStepDelayMs) {
    return;
  }
  lastStepMs_ = now;

  const int step = (target_ > current_) ? 1 : -1;
  current_ += step;
  servo_.write(current_);

  if (current_ == target_) {
    moving_ = false;
  }
}

}  // namespace sentinel
