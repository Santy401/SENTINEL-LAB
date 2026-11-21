#pragma once

#include <stddef.h>
#include <stdint.h>

namespace sentinel {
namespace v02 {
namespace settings {

// ---- Wi-Fi Access Point ----
// SSID/password are provided by the gitignored include/secrets.h file.
constexpr uint8_t kApChannel        = 1;
constexpr uint8_t kApMaxConnections = 4;
constexpr bool    kApHidden         = false;
constexpr char    kMdnsName[]       = "sentinel";

// ---- HTTP ----
constexpr uint16_t kHttpPort = 80;

// ---- Motion detection defaults (SI units: m/s^2) ----
constexpr float    kDefaultMovementThreshold = 2.0f;
constexpr float    kDefaultAlertThreshold    = 6.0f;
constexpr float    kDefaultHysteresis        = 0.8f;
constexpr float    kDefaultGravityAlpha      = 0.92f;
constexpr uint8_t  kDefaultConfirmations     = 3;
constexpr uint32_t kDefaultTimeoutMs         = 2000UL;

// ---- Safety / limits ----
constexpr size_t   kMaxEvents        = 24;   // engine ring buffer
constexpr size_t   kMaxEventsInJson  = 8;    // telemetry payload
constexpr uint32_t kMinPostIntervalMs = 50UL; // client-side throttle hint

// ---- Serial ----
constexpr unsigned long kSerialBaud   = 115200UL;
constexpr unsigned long kHeartbeatMs  = 5000UL;
constexpr unsigned long kSerialWaitMs = 1200UL;

}  // namespace settings
}  // namespace v02
}  // namespace sentinel
