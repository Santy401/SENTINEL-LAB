#include <Arduino.h>
#include "v02/led_indicator.h"
#include "v02/motion_engine.h"
#include "v02/web_dashboard.h"
#include "v02/wifi_ap.h"
#include "rf/rf_monitor.h"
using namespace sentinel::v02;
namespace { MotionEngine engine; LedIndicator leds; WiFiApInfo wifi; WebDashboard dashboard; }
void setup() {
  Serial.begin(115200);
  delay(1200);
  Serial.println("SENTINEL RF 0.3.1 | HC-SR04/servo excluded | LEDs VIRTUAL");
  engine.begin(MotionThresholds{}, millis()); leds.begin();
  if (!startAccessPoint(&wifi)) Serial.println("AP startup failed");
  sentinel::rf::beginRf();
  dashboard.begin(engine, leds, wifi);
}
void loop() {
  const uint32_t now = millis();
  sentinel::rf::updateRf(now);
  engine.update(now);
  sentinel::rf::observeMotion(static_cast<int>(engine.state()), engine.snapshot().source == AccelSource::Simulated, now);
  leds.update(engine.state()); refreshAccessPoint(&wifi);
  dashboard.handleClient(); delay(2);
}
