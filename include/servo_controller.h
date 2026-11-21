#pragma once

#include <Arduino.h>
#include <ESP32Servo.h>

#include "config.h"

namespace sentinel {

// SG90 wrapper with safe limits and progressive, non-blocking movement.
class ServoController {
 public:
  bool begin(uint8_t pin, int homeAngle, int protectAngle);
  void setTarget(int angle);
  void update();

  bool isReady() const { return ready_; }
  bool isMoving() const { return moving_; }
  int currentAngle() const { return current_; }

 private:
  Servo servo_;

  uint8_t pin_ = 0;
  int home_ = 0;
  int protect_ = 0;
  int min_ = config::kServoMinAngle;
  int max_ = config::kServoMaxAngle;
  int current_ = 0;
  int target_ = 0;

  bool ready_ = false;
  bool moving_ = false;

  unsigned long lastStepMs_ = 0;
  unsigned long moveStartMs_ = 0;
};

}  // namespace sentinel
