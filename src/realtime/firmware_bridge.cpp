#include "realtime/firmware_bridge.h"

#include "realtime/status_policy.h"
#include "rf/realtime_access.h"
#include "v02/web_dashboard.h"

#include <Arduino.h>

#include <cmath>
#include <stdio.h>
#include <string.h>

namespace sentinel {
namespace realtime {
namespace {

const char* sourceTypeName(v02::AccelSource source) {
  switch (source) {
    case v02::AccelSource::BrowserDeviceMotion:
      return "BrowserDeviceMotion";
    case v02::AccelSource::NativeClient:
      return "NativeClient";
    case v02::AccelSource::Simulated:
      return "Simulated";
    default:
      return "None";
  }
}

}  // namespace

size_t snapshotRfClients(RfClientView* output, size_t capacity,
                         uint8_t& channel, bool& available) {
  channel = 0;
  available = false;
  if (!output || capacity == 0) return 0;

  const rf::RfMonitor& monitor = rf::realtimeMonitor();
  const rf::Laboratory& lab = rf::realtimeLaboratory();
  available = monitor.available();
  channel = monitor.channel();

  size_t written = 0;
  for (size_t i = 0; i < 8 && written < capacity; ++i) {
    const rf::Client& client = monitor.clients()[i];
    if (!client.id) continue;

    RfClientView& view = output[written++];
    view = RfClientView{};
    view.clientId = client.id;
    view.connected = client.connected;
    view.lastSeenMs = client.lastSeenMs;
    if (client.rssiValid) view.rssiDbm = static_cast<double>(client.rssi);
    if (client.filter.ready()) {
      view.filteredRssiDbm = client.filter.value();
    } else {
      view.filteredRssiDbm = view.rssiDbm;
    }

    for (size_t j = lab.measurementCount; j-- > 0;) {
      const rf::Measurement& measurement = lab.measurements[j];
      if (measurement.clientId != client.id ||
          !std::isfinite(measurement.estimatedM)) {
        continue;
      }
      bool calibrated = false;
      for (size_t k = 0; k < lab.experimentCount; ++k) {
        if (lab.experiments[k].id == measurement.experimentId) {
          calibrated = lab.experiments[k].model.calibrated();
          break;
        }
      }
      view.hasDistance = true;
      view.distanceM = measurement.estimatedM;
      view.distanceCalibrated = calibrated;
      break;
    }
  }
  return written;
}

bool snapshotMotionStatus(uint32_t nowMs, MotionStatusView& output) {
  const v02::MotionEngine& engine = v02::realtimeMotionEngine();
  const v02::AccelSource source = engine.snapshot().source;
  if (source == v02::AccelSource::Simulated ||
      source == v02::AccelSource::None) {
    return false;
  }

  const rf::MotionIngestSnapshot snapshot =
      v02::realtimeMotionIngest().snapshot(nowMs);
  static char deviceId[sizeof(snapshot.allowedDeviceId)]{};
  deviceId[0] = '\0';
  if (snapshot.allowedDeviceId[0]) {
    snprintf(deviceId, sizeof(deviceId), "%s", snapshot.allowedDeviceId);
  } else if (snapshot.lastDeviceId[0]) {
    snprintf(deviceId, sizeof(deviceId), "%s", snapshot.lastDeviceId);
  }
  if (!validProtocolDeviceId(deviceId)) return false;

  output = MotionStatusView{};
  output.deviceId = deviceId;
  output.sourceType = sourceTypeName(source);
  output.paired = snapshot.authorized;
  output.acceptedSamples = snapshot.acceptedSamples;
  output.lastSampleAgeMs = snapshot.sampleAgeMs;
  output.signalLost = snapshot.signalLost;
#ifdef SENTINEL_REALTIME
  output.simulationRejected = snapshot.simulationRejected;
#endif
  if (snapshot.lastHttpCode >= 100 && snapshot.lastHttpCode <= 599) {
    output.lastHttpCode = static_cast<uint16_t>(snapshot.lastHttpCode);
  }
  return true;
}

const char* firmwareBootId() { return rf::realtimeBootId(); }

const rf::EventEngine& firmwareEvents() { return rf::realtimeEvents(); }

uint32_t firmwareFreeHeapBytes() {
  return static_cast<uint32_t>(ESP.getFreeHeap());
}

}  // namespace realtime
}  // namespace sentinel
