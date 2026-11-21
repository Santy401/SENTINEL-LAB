#pragma once

#include <stddef.h>
#include <stdint.h>

namespace sentinel {
namespace config {

// ---- Hardware pins (ESP32-S3, native USB on GPIO19/20) ----
constexpr uint8_t kTriggerPin = 4;   // HC-SR04 TRIG, 3.3 V output
constexpr uint8_t kEchoPin    = 5;   // HC-SR04 ECHO through divider -> 3.3 V
constexpr uint8_t kServoPin   = 6;   // SG90 PWM via LEDC/MCPWM
constexpr uint8_t kSparePin   = 7;   // reserved for future LCD/LED

// ---- Serial / timing ----
constexpr unsigned long kSerialBaud      = 115200UL;
constexpr unsigned long kHeartbeatMs     = 2000UL;
constexpr unsigned long kSensorIntervalMs = 250UL;

// ---- Ultrasonic sensor ----
constexpr float         kSpeedOfSoundCmPerUs = 0.0343f;  // 343 m/s
constexpr unsigned long kEchoTimeoutUs       = 30000UL;  // ~5 m limit
constexpr size_t        kSampleCount         = 5;
constexpr float         kMinValidDistanceCm  = 2.0f;
constexpr float         kMaxValidDistanceCm  = 400.0f;

// ---- Detection logic (hysteresis) ----
constexpr float  kIntrusionThresholdCm   = 20.0f;
constexpr float  kClearThresholdCm       = 30.0f;
constexpr uint8_t kIntrusionConfirmations = 3;
constexpr uint8_t kClearConfirmations     = 3;
constexpr uint8_t kInvalidBeforeError     = 10;

// ---- Servo ----
constexpr int           kServoMinPulseUs   = 500;
constexpr int           kServoMaxPulseUs   = 2400;
constexpr int           kServoFrequencyHz  = 50;
constexpr int           kServoHomeAngle    = 0;
constexpr int           kServoProtectAngle = 90;
constexpr int           kServoMinAngle     = 0;
constexpr int           kServoMaxAngle     = 120;
constexpr unsigned long kServoStepDelayMs  = 15UL;
constexpr unsigned long kServoMoveTimeoutMs = 2000UL;

// Safety gate: keep false until external 5 V servo supply is verified.
constexpr bool kServoEnabledByDefault = false;

}  // namespace config
}  // namespace sentinel
