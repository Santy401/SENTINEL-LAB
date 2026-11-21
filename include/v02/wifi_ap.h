#pragma once

#include <Arduino.h>

namespace sentinel {
namespace v02 {

struct WiFiApInfo {
  bool started = false;
  String ssid;
  String ip;
  uint8_t clients = 0;
};

// Starts an AP using SENTINEL_WIFI_SSID / SENTINEL_WIFI_PASSWORD from the
// gitignored secrets.h. Returns false only if softAP could not be enabled.
bool startAccessPoint(WiFiApInfo* info);
void refreshAccessPoint(WiFiApInfo* info);

}  // namespace v02
}  // namespace sentinel
