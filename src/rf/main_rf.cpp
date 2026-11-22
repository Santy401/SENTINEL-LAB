#include <Arduino.h>
#include "v02/led_indicator.h"
#include "v02/motion_engine.h"
#include "v02/web_dashboard.h"
#include "v02/wifi_ap.h"
#include "rf/rf_monitor.h"
using namespace sentinel::v02;
namespace { MotionEngine engine; LedIndicator leds; WiFiApInfo wifi; WebDashboard dashboard; }
#ifdef SENTINEL_REALTIME
#include "realtime/auth.h"
#include "realtime/firmware_bridge.h"
#include "realtime/realtime_core.h"
#include "rf/realtime_access.h"
#if __has_include("secrets.h")
#include "secrets.h"
#endif
#ifndef SENTINEL_REALTIME_BEARER_TOKEN
#define SENTINEL_REALTIME_BEARER_TOKEN ""
#endif
namespace {
sentinel::realtime::RealtimeCore realtime;
const sentinel::realtime::AuthGuard realtimeAuth(SENTINEL_REALTIME_BEARER_TOKEN);
}
namespace sentinel { namespace v02 {
const MotionEngine& realtimeMotionEngine() { return engine; }
} }
#endif
void setup() {
  Serial.begin(115200);
  delay(1200);
  Serial.println("SENTINEL RF 0.3.1 | HC-SR04/servo excluded | LEDs VIRTUAL");
  engine.begin(MotionThresholds{}, millis()); leds.begin();
  if (!startAccessPoint(&wifi)) Serial.println("AP startup failed");
  sentinel::rf::beginRf();
  dashboard.begin(engine, leds, wifi);
#ifdef SENTINEL_REALTIME
  Serial.printf("SENTINEL realtime core %s | WebSocket :81 | reset=%s\n",
                sentinel::realtime::kFirmwareVersion,
                sentinel::realtime::firmwareResetReason());
  realtime.begin(81, sentinel::rf::realtimeBootId(), realtimeAuth);
#endif
}
void loop() {
  const uint32_t now = millis();
  sentinel::rf::updateRf(now);
  engine.update(now);
  sentinel::rf::observeMotion(static_cast<int>(engine.state()), engine.snapshot().source == AccelSource::Simulated, now);
  leds.update(engine.state()); refreshAccessPoint(&wifi);
  dashboard.handleClient(); delay(2);
#ifdef SENTINEL_REALTIME
  realtime.loop(now);
#endif
}
