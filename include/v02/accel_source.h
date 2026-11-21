#pragma once

#include <stdint.h>

namespace sentinel {
namespace v02 {

// Identifies where an acceleration sample came from. The dashboard must always
// label Simulated samples as simulated; they are never presented as real data.
enum class AccelSource : uint8_t {
  None                = 0,
  BrowserDeviceMotion = 1,
  Simulated           = 2,
  NativeClient        = 3
};

struct AccelSample {
  float x            = 0.0f;  // m/s^2, includes gravity when available
  float y            = 0.0f;
  float z            = 0.0f;
  uint32_t timestampMs = 0;   // assigned by the ESP32 on reception
  AccelSource source = AccelSource::None;
};

inline const char* accelSourceName(AccelSource source) {
  switch (source) {
    case AccelSource::BrowserDeviceMotion: return "BrowserDeviceMotion";
    case AccelSource::Simulated:           return "Simulated";
    case AccelSource::NativeClient:        return "NativeClient";
    default:                               return "None";
  }
}

}  // namespace v02
}  // namespace sentinel
