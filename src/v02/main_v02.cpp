#include <Arduino.h>

#include "v02/led_indicator.h"
#include "v02/motion_engine.h"
#include "v02/settings.h"
#include "v02/web_dashboard.h"
#include "v02/wifi_ap.h"

using namespace sentinel::v02;

namespace {

MotionEngine engine;
LedIndicator leds;
WiFiApInfo wifi;
WebDashboard dashboard;
uint32_t lastHeartbeatMs = 0;

void printBanner() {
  Serial.println();
  Serial.println(F("=== SENTINEL firmware v0.2.0 ==="));
  Serial.printf("Chip      : %s (rev %u, cores %u)\n", ESP.getChipModel(),
                ESP.getChipRevision(), ESP.getChipCores());
  Serial.printf("CPU       : %lu MHz\n",
                static_cast<unsigned long>(ESP.getCpuFreqMHz()));
  Serial.printf("Flash     : %lu bytes\n",
                static_cast<unsigned long>(ESP.getFlashChipSize()));
#if defined(BOARD_HAS_PSRAM)
  Serial.printf("PSRAM     : OK, size=%lu bytes, free=%lu bytes\n",
                static_cast<unsigned long>(ESP.getPsramSize()),
                static_cast<unsigned long>(ESP.getFreePsram()));
#else
  Serial.println(F("PSRAM     : not enabled"));
#endif
  Serial.printf("Heap      : free=%lu bytes\n",
                static_cast<unsigned long>(ESP.getFreeHeap()));
  Serial.println(F("HC-SR04   : DISCONNECTED (ECHO not used in v0.2.0)"));
  Serial.println(F("Servo     : DISABLED (no PWM output)"));
  Serial.println(F("Indicators: VIRTUAL (no GPIO assigned)"));
}

}  // namespace

void setup() {
  Serial.begin(settings::kSerialBaud);
  delay(settings::kSerialWaitMs);
  printBanner();

  MotionThresholds thresholds;  // all values from settings.h
  engine.begin(thresholds, millis());
  leds.begin();

  if (!startAccessPoint(&wifi)) {
    Serial.println(F("[FATAL] Wi-Fi AP could not start"));
  }

  dashboard.begin(engine, leds, wifi);

  Serial.println(F("[APP] Ready"));
  Serial.printf("[APP] Dashboard: http://%s/\n", wifi.ip.c_str());
  Serial.printf("[APP] mDNS    : http://%s.local\n", settings::kMdnsName);
  Serial.println(F("[APP] API     : /api/state  /api/accel  /api/config"));
}

void loop() {
  dashboard.handleClient();

  const uint32_t now = millis();
  engine.update(now);
  refreshAccessPoint(&wifi);
  leds.update(engine.state());

  if ((now - lastHeartbeatMs) >= settings::kHeartbeatMs) {
    lastHeartbeatMs = now;
    const MotionSnapshot& s = engine.snapshot();
    Serial.printf(
        "[HB] state=%s fresh=%s source=%s linear=%.2f samples=%lu clients=%u "
        "heap=%lu\n",
        motionStateName(s.state), engine.fresh(now) ? "yes" : "no",
        accelSourceName(s.source), s.linearMagnitude,
        static_cast<unsigned long>(s.sampleCount), wifi.clients,
        static_cast<unsigned long>(ESP.getFreeHeap()));
  }

  delay(2);
}
