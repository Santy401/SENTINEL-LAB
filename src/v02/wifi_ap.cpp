#include "v02/wifi_ap.h"

#include "v02/settings.h"
#include "secrets.h"

#if !defined(SENTINEL_WIFI_SSID) || !defined(SENTINEL_WIFI_PASSWORD)
#error "Create include/secrets.h from secrets.example.h before building v0.2.0."
#endif

static_assert(sizeof(SENTINEL_WIFI_PASSWORD) - 1 >= 8,
              "SENTINEL_WIFI_PASSWORD must be at least 8 characters (WPA2)");

#include <ESPmDNS.h>
#include <WiFi.h>

namespace sentinel {
namespace v02 {

namespace {

bool looksLikePlaceholder(const char* value) {
  const String text(value);
  return text.indexOf("CAMBIAR") >= 0 || text.indexOf("CHANGE") >= 0;
}

}  // namespace

bool startAccessPoint(WiFiApInfo* info) {
  if (info == nullptr) {
    return false;
  }

  info->ssid = SENTINEL_WIFI_SSID;
  info->ip = "0.0.0.0";

  if (looksLikePlaceholder(SENTINEL_WIFI_SSID) ||
      looksLikePlaceholder(SENTINEL_WIFI_PASSWORD)) {
    Serial.println(F("[WARN] secrets.h still has placeholder credentials"));
  }

  WiFi.mode(WIFI_AP);
  WiFi.setSleep(false);

  const bool ok = WiFi.softAP(SENTINEL_WIFI_SSID, SENTINEL_WIFI_PASSWORD,
                              settings::kApChannel, settings::kApHidden,
                              settings::kApMaxConnections);
  if (!ok) {
    Serial.println(F("[WIFI] softAP failed to start"));
    info->started = false;
    return false;
  }

  delay(200);
  info->ip = WiFi.softAPIP().toString();
  info->clients = WiFi.softAPgetStationNum();
  info->started = true;

  Serial.printf("[WIFI] AP SSID : %s\n", info->ssid.c_str());
  Serial.printf("[WIFI] AP IP   : %s\n", info->ip.c_str());
  Serial.printf("[WIFI] Clients: %u\n", info->clients);

  if (MDNS.begin(settings::kMdnsName)) {
    MDNS.addService("http", "tcp", settings::kHttpPort);
    Serial.printf("[WIFI] mDNS   : http://%s.local\n", settings::kMdnsName);
  } else {
    Serial.println(F("[WARN] mDNS not available; use the IP address"));
  }

  return true;
}

void refreshAccessPoint(WiFiApInfo* info) {
  if (info == nullptr || !info->started) {
    return;
  }
  info->clients = WiFi.softAPgetStationNum();
}

}  // namespace v02
}  // namespace sentinel
