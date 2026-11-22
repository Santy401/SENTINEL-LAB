#pragma once

#include <stddef.h>

namespace sentinel {
namespace realtime {

// Motion states are not part of the v1 system.status enum. The mapping below is
// shared by the firmware producer and the host fixture generator so a schema
// drift cannot be hidden by sanitized test constants.
enum class OperationalState {
  Booting,
  Idle,
  Monitoring,
  Alert,
  SensorOffline,
  Error,
};

inline const char* operationalStateName(OperationalState state) {
  switch (state) {
    case OperationalState::Booting:
      return "BOOTING";
    case OperationalState::Idle:
      return "IDLE";
    case OperationalState::Alert:
      // A motion alert is reported in motion.status and event messages; the
      // system itself keeps monitoring.
      return "MONITORING";
    case OperationalState::SensorOffline:
    case OperationalState::Error:
      return "ERROR";
    case OperationalState::Monitoring:
    default:
      return "MONITORING";
  }
}

struct CapabilityPolicy {
  const char* const* values;
  size_t count;
};

inline CapabilityPolicy systemCapabilityPolicy() {
  static const char* const capabilities[] = {"rf.monitor", "motion.ingest",
                                             "laboratory"};
  return {capabilities, sizeof(capabilities) / sizeof(capabilities[0])};
}

// Protocol v1 requires this identifier pattern. HTTP v0.3.1 accepts a wider
// range of deviceId values, so v1 omits motion.status rather than rewriting an
// identity or inventing a new name.
inline bool validProtocolDeviceId(const char* id) {
  if (!id || !*id) return false;
  size_t length = 0;
  while (id[length]) {
    if (++length > 64) return false;
  }
  const char first = id[0];
  const bool firstValid = (first >= 'a' && first <= 'z') ||
                          (first >= 'A' && first <= 'Z') ||
                          (first >= '0' && first <= '9');
  if (!firstValid) return false;
  for (size_t i = 1; i < length; ++i) {
    const char c = id[i];
    const bool valid = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                       (c >= '0' && c <= '9') || c == '.' || c == '_' ||
                       c == ':' || c == '-';
    if (!valid) return false;
  }
  return true;
}

}  // namespace realtime
}  // namespace sentinel
