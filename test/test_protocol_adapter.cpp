#include "realtime/protocol_adapter.h"
#include "realtime/status_policy.h"

#include <ArduinoJson.h>

#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>

using namespace sentinel::realtime;

namespace {

constexpr char kSource[] = "esp32-s3-realtime";
constexpr char kSession[] = "realtime-boot-0001";

void assertValidEnvelope(const char* json, const char* type) {
  JsonDocument document;
  assert(!deserializeJson(document, json));
  assert(document["protocolVersion"] == kProtocolVersion);
  assert(document["type"] == type);
  assert(document["sequence"] == 7);
  assert(document["source"] == kSource);
  assert(document["sessionId"] == kSession);
  assert(!document["payload"].isNull());
}

void writeFixture(const char* name, const char* json) {
  const std::string path = std::string("build/") + name;
  FILE* file = fopen(path.c_str(), "w");
  assert(file);
  fputs(json, file);
  fclose(file);
}

}  // namespace

int main() {
  UtcClock clock;
  char hello[kProtocolMessageBytes]{};
  char heartbeat[kProtocolMessageBytes]{};
  char telemetry[kProtocolMessageBytes]{};
  char status[kProtocolMessageBytes]{};
  char event[kProtocolMessageBytes]{};
  char error[kProtocolMessageBytes]{};
  char motion[kProtocolMessageBytes]{};
  char goodbye[kProtocolMessageBytes]{};

  // No wall-clock time: no Protocol v1 message may be produced.
  ProtocolAdapter adapter(clock);
  assert(!adapter.buildHeartbeat(kSource, kSession, 7, 1000, "boot-0001",
                                 heartbeat, sizeof(heartbeat)));

  clock.sync(1795277402100LL, 1000);

  const char* const capabilities[] = {"rf.monitor", "motion.ingest"};
  assert(adapter.buildHello(kSource, kSession, 7, 1000, "sentinel-lab-01",
                            "firmware", capabilities, 2, hello, sizeof(hello)));
  assertValidEnvelope(hello, "connection.hello");
  JsonDocument parsed;
  deserializeJson(parsed, hello);
  assert(parsed["payload"]["role"] == "firmware");
  assert(parsed["payload"]["supportedVersion"] == kProtocolVersion);
  assert(parsed["payload"]["capabilities"].size() == 2);

  assert(adapter.buildHeartbeat(kSource, kSession, 7, 2500, "boot-0001",
                                heartbeat, sizeof(heartbeat)));
  assertValidEnvelope(heartbeat, "connection.heartbeat");
  deserializeJson(parsed, heartbeat);
  assert(parsed["payload"]["bootId"] == "boot-0001");
  assert(parsed["payload"]["uptimeMs"] == 2500);

  RfClientView clients[2]{};
  clients[0].clientId = 3;
  clients[0].connected = true;
  clients[0].rssiDbm = -54;
  clients[0].filteredRssiDbm = -56.4;
  clients[0].lastSeenMs = 2400;
  clients[0].hasDistance = true;
  clients[0].distanceM = 1.83;
  clients[0].distanceCalibrated = true;
  clients[1].clientId = 4;
  clients[1].connected = false;
  clients[1].lastSeenMs = 2000;
  assert(adapter.buildRfTelemetry(kSource, kSession, 7, 2500, "boot-0001",
                                  1, clients, 2, telemetry, sizeof(telemetry)));
  assertValidEnvelope(telemetry, "rf.telemetry");
  deserializeJson(parsed, telemetry);
  assert(parsed["payload"]["clientCount"] == 2);
  assert(parsed["payload"]["clients"].size() == 2);
  assert(parsed["payload"]["clients"][0]["rssiDbm"] == -54);
  assert(parsed["payload"]["clients"][0]["filteredRssiDbm"] == -56);
  assert(parsed["payload"]["clients"][1]["rssiDbm"].isNull());
  assert(parsed["payload"]["clients"][0]["lastSeen"] ==
         "2026-11-21T16:10:03.500Z");
  assert(parsed["payload"]["clients"][0]["estimatedDistance"]["calibrated"] ==
         true);

  // Use the exact policy consumed by RealtimeCore, not sanitized constants.
  assert(std::strcmp(operationalStateName(OperationalState::Alert),
                     "MONITORING") == 0);
  assert(std::strcmp(operationalStateName(OperationalState::SensorOffline),
                     "ERROR") == 0);
  const CapabilityPolicy policyCapabilities = systemCapabilityPolicy();
  assert(policyCapabilities.count == 3);
  assert(validProtocolDeviceId("iphone-12.sensorlogger:01"));
  assert(!validProtocolDeviceId("iPhone 12"));

  SystemStatusView systemStatus;
  systemStatus.uptimeMs = 2500;
  systemStatus.operationalState =
      operationalStateName(OperationalState::Alert);
  systemStatus.capabilities = policyCapabilities.values;
  systemStatus.capabilityCount = policyCapabilities.count;
  systemStatus.freeHeapBytes = 120000;
  assert(adapter.buildSystemStatus(kSource, kSession, 7, 2500, "boot-0001",
                                   systemStatus, status, sizeof(status)));
  assertValidEnvelope(status, "system.status");

  MotionStatusView motionStatus;
  motionStatus.deviceId = "iphone-12-sensorlogger-01";
  motionStatus.sourceType = "NativeClient";
  motionStatus.paired = true;
  motionStatus.acceptedSamples = 42;
  motionStatus.lastSampleAgeMs = 75;
  motionStatus.signalLost = false;
  motionStatus.simulationRejected = 3;
  motionStatus.lastHttpCode = 200;
  assert(adapter.buildMotionStatus(kSource, kSession, 7, 2500, motionStatus,
                                   motion, sizeof(motion)));
  assertValidEnvelope(motion, "motion.status");
  MotionStatusView invalidMotion = motionStatus;
  invalidMotion.deviceId = "iPhone 12";
  char rejectedMotion[kProtocolMessageBytes]{};
  assert(!adapter.buildMotionStatus(kSource, kSession, 7, 2500, invalidMotion,
                                    rejectedMotion, sizeof(rejectedMotion)));

  EventDetailsView details;
  details.clientId = 3;
  details.rssiDbm = -87;
  details.message = "RSSI below weak threshold";
  assert(adapter.buildEvent(kSource, kSession, 7, 2500, "evt-0007",
                            "SIGNAL_WEAK", 2400, details, event,
                            sizeof(event)));
  assertValidEnvelope(event, "event");
  deserializeJson(parsed, event);
  assert(parsed["payload"]["occurredAt"] == "2026-11-21T16:10:03.500Z");
  assert(parsed["payload"]["details"]["clientId"] == 3);
  assert(parsed["payload"]["details"]["rssiDbm"] == -87);

  assert(adapter.buildError(kSource, kSession, 7, 2500, "INVALID_ENVELOPE",
                            "malformed envelope", false, "web-dashboard", 6,
                            error, sizeof(error)));
  assertValidEnvelope(error, "error");
  deserializeJson(parsed, error);
  assert(parsed["payload"]["relatedSource"] == "web-dashboard");
  assert(parsed["payload"]["relatedSequence"] == 6);

  assert(adapter.buildGoodbye(kSource, kSession, 7, 2500, "client-request",
                              goodbye, sizeof(goodbye)));
  assertValidEnvelope(goodbye, "connection.goodbye");

  // Relayed phone motion sample (firmware 0.5.0).
  MotionSampleView sample;
  sample.deviceId = "iphone-0a1b2c3d";
  sample.sourceType = "CoreMotion";
  sample.sampleIndex = 42;
  sample.sensorTimestamp = "2026-11-21T16:10:02.000Z";
  sample.acceleration = MotionVector{1.2, -9.6, 0.4};
  sample.gravity = MotionVector{0.1, -9.8, 0.2};
  sample.linear = MotionVector{1.1, 0.2, 0.2};
  sample.state = "MOVEMENT";
  char motionSample[kProtocolMessageBytes]{};
  assert(adapter.buildMotionSample(kSource, kSession, 8, 2500, sample,
                                   motionSample, sizeof(motionSample)));
  assert(std::strstr(motionSample, "\"type\":\"motion.sample\""));
  assert(std::strstr(motionSample, "\"simulated\":false"));
  MotionSampleView badSample = sample;
  badSample.deviceId = "bad id";
  assert(!adapter.buildMotionSample(kSource, kSession, 9, 2500, badSample,
                                    motionSample, sizeof(motionSample)));
  assert(adapter.buildMotionSample(kSource, kSession, 8, 2500, sample,
                                   motionSample, sizeof(motionSample)));
  writeFixture("firmware-motion-sample.json", motionSample);
  writeFixture("firmware-hello.json", hello);
  writeFixture("firmware-heartbeat.json", heartbeat);
  writeFixture("firmware-rf.json", telemetry);
  writeFixture("firmware-system.json", status);
  writeFixture("firmware-motion.json", motion);
  writeFixture("firmware-event.json", event);
  writeFixture("firmware-error.json", error);
  writeFixture("firmware-goodbye.json", goodbye);

  std::puts("test_protocol_adapter: ALL TESTS PASSED");
}
