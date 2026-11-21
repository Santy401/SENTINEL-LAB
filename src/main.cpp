#include <Arduino.h>

#include "config.h"
#include "diagnostic_mode.h"
#include "distance_sensor.h"
#include "servo_controller.h"

using namespace sentinel;

enum class State {
  BOOTING,
  IDLE,
  MONITORING,
  INTRUSION_DETECTED,
  ACTUATING,
  ERROR
};

static DistanceSensor distanceSensor;
static ServoController servo;

static State state = State::BOOTING;
static bool returnRequested = false;
static uint8_t nearCount = 0;
static uint8_t clearCount = 0;
static uint8_t invalidCount = 0;
static unsigned long stateMs = 0;
static unsigned long lastHeartbeatMs = 0;
static bool newValidSample = false;

static const char* stateName(State s) {
  switch (s) {
    case State::BOOTING:            return "BOOTING";
    case State::IDLE:               return "IDLE";
    case State::MONITORING:         return "MONITORING";
    case State::INTRUSION_DETECTED: return "INTRUSION_DETECTED";
    case State::ACTUATING:          return "ACTUATING";
    case State::ERROR:              return "ERROR";
    default:                        return "UNKNOWN";
  }
}

static void setState(State next) {
  if (next == state) {
    return;
  }
  Serial.printf("[STATE] %s -> %s\n", stateName(state), stateName(next));
  state = next;
  stateMs = millis();
  if (next == State::ERROR) {
    diagnostic::printSummary();
  }
}

static void logEvent(const char* message) {
  Serial.printf("[EVENT][%s] %s\n", stateName(state), message);
}

static void printBoardInfo() {
  Serial.println();
  Serial.println(F("=== SENTINEL firmware v0.1.0 ==="));
  Serial.printf("Chip      : %s (rev %u, cores %d)\n",
                ESP.getChipModel(), ESP.getChipRevision(), ESP.getChipCores());
  Serial.printf("CPU       : %lu MHz\n",
                static_cast<unsigned long>(ESP.getCpuFreqMHz()));
  Serial.printf("Flash     : %lu bytes\n",
                static_cast<unsigned long>(ESP.getFlashChipSize()));

  if (psramFound()) {
    Serial.printf("PSRAM     : OK, size=%lu bytes, free=%lu bytes\n",
                  static_cast<unsigned long>(ESP.getPsramSize()),
                  static_cast<unsigned long>(ESP.getFreePsram()));
  } else {
    Serial.println(F("PSRAM     : NOT FOUND / NOT INITIALIZED"));
  }

  Serial.printf("Heap      : free=%lu bytes, min=%lu bytes\n",
                static_cast<unsigned long>(ESP.getFreeHeap()),
                static_cast<unsigned long>(ESP.getMinFreeHeap()));
  Serial.println();
}

void setup() {
  Serial.begin(config::kSerialBaud);
  const unsigned long serialStart = millis();
  while (!Serial && (millis() - serialStart < 2000UL)) {
    delay(10);
  }

  printBoardInfo();
#if defined(SENTINEL_DIAGNOSTIC_MODE)
  Serial.println(F("DIAG MODE  : ENABLED (servo remains disabled)"));
#endif

  distanceSensor.begin(config::kTriggerPin, config::kEchoPin);
  Serial.printf("HC-SR04   : TRIG=GPIO%u ECHO=GPIO%u (divider required)\n",
                config::kTriggerPin, config::kEchoPin);

  if (config::kServoEnabledByDefault) {
    servo.begin(config::kServoPin, config::kServoHomeAngle,
                config::kServoProtectAngle);
    Serial.printf("SG90      : ENABLED on GPIO%u\n", config::kServoPin);
  } else {
    Serial.printf("SG90      : DISABLED on GPIO%u (verify 5 V supply first)\n",
                  config::kServoPin);
  }

  setState(State::IDLE);
}

void loop() {
  distanceSensor.update();
  servo.update();

  newValidSample = false;

  if (millis() - lastHeartbeatMs >= config::kHeartbeatMs) {
    lastHeartbeatMs = millis();
    Serial.printf("[HB] state=%s dist=%.1fcm valid=%s near=%u clear=%u servo=%s\n",
                  stateName(state),
                  distanceSensor.distanceCm(),
                  distanceSensor.lastValid() ? "yes" : "no",
                  nearCount,
                  clearCount,
                  servo.isReady() ? (servo.isMoving() ? "moving" : "ready")
                                  : "disabled");
  }

  if (distanceSensor.hasSample()) {
    const bool valid = distanceSensor.lastValid();
    const float cm = distanceSensor.distanceCm();
    diagnostic::report(distanceSensor);

    if (state == State::MONITORING || state == State::ACTUATING ||
        state == State::INTRUSION_DETECTED || state == State::ERROR) {
      if (valid) {
        invalidCount = 0;
        newValidSample = true;
        Serial.printf("[DIST] %.1f cm\n", cm);

        if (cm < config::kIntrusionThresholdCm) {
          ++nearCount;
          clearCount = 0;
          if (state == State::MONITORING &&
              nearCount >= config::kIntrusionConfirmations) {
            nearCount = 0;
            logEvent("intrusion confirmed (<20 cm x3)");
            setState(State::INTRUSION_DETECTED);
          }
        } else if (cm > config::kClearThresholdCm) {
          ++clearCount;
          nearCount = 0;
        } else {
          // Hysteresis band: do not change counters.
          nearCount = 0;
          clearCount = 0;
        }
      } else {
        ++invalidCount;
        Serial.println(F("[DIST] invalid reading (ignored)"));
        if (invalidCount >= config::kInvalidBeforeError &&
            state != State::ERROR) {
          logEvent("too many invalid readings");
          setState(State::ERROR);
        }
      }
    }

    distanceSensor.clearSample();
  }

  switch (state) {
    case State::BOOTING:
      break;

    case State::IDLE:
      if (millis() - stateMs >= 500UL) {
        logEvent("monitoring started");
        setState(State::MONITORING);
      }
      break;

    case State::MONITORING:
      break;

    case State::INTRUSION_DETECTED:
      if (servo.isReady()) {
        logEvent("moving servo to protect position");
        servo.setTarget(config::kServoProtectAngle);
        setState(State::ACTUATING);
      } else {
        logEvent("actuation blocked: servo power not confirmed");
        if (clearCount >= config::kClearConfirmations) {
          clearCount = 0;
          setState(State::MONITORING);
        }
      }
      break;

    case State::ACTUATING:
      if (!servo.isMoving() && !returnRequested) {
        if (clearCount >= config::kClearConfirmations) {
          clearCount = 0;
          returnRequested = true;
          logEvent("clear confirmed (>30 cm x3), returning home");
          servo.setTarget(config::kServoHomeAngle);
        }
      }
      if (returnRequested && !servo.isMoving()) {
        returnRequested = false;
        logEvent("servo at home position");
        setState(State::MONITORING);
      }
      break;

    case State::ERROR:
      // Recovery requires a NEW valid sample, not the cached lastValid() flag.
      if (newValidSample) {
        invalidCount = 0;
        logEvent("sensor recovered");
        setState(State::MONITORING);
      }
      break;
  }
}
