#include "realtime/protocol_adapter.h"
#include "realtime/session.h"

#include <ArduinoJson.h>

#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>

using namespace sentinel::realtime;

namespace {

std::string hello(const char* role, const char* version, uint64_t sequence) {
  JsonDocument document;
  JsonObject payload = document["payload"].to<JsonObject>();
  document["protocolVersion"] = kProtocolVersion;
  document["type"] = "connection.hello";
  document["timestamp"] = "2026-11-21T16:10:02.100Z";
  document["sequence"] = sequence;
  document["source"] = "web-dashboard";
  payload["deviceId"] = "web-01";
  payload["role"] = role;
  payload["supportedVersion"] = version;
  payload["capabilities"].to<JsonArray>().add("telemetry");
  std::string text;
  serializeJson(document, text);
  return text;
}

}  // namespace

int main() {
  SessionState session;
  session.begin(2, "boot1234");
  assert(std::strncmp(session.sessionId(), "boot1234-p2-", 12) == 0);

  SessionState reconnected;
  reconnected.begin(2, "boot1234");
  assert(std::strcmp(reconnected.sessionId(), session.sessionId()) != 0);

  SessionState system;
  system.begin(0, "boot1234", true);
  assert(std::strncmp(system.sessionId(), "boot1234-s", 10) == 0);
  assert(std::strcmp(system.sessionId(), session.sessionId()) != 0);

  assert(session.nextSequence() == 1);
  assert(session.nextSequence() == 2);

  InboundValidator validator;
  const std::string webHello = hello("web", kProtocolVersion, 1);
  InboundMessage result = validator.validate(
      webHello.data(), webHello.size(), 1000, false, session);
  assert(result.valid);
  assert(!result.syncClock);
  assert(std::strcmp(result.role, "web") == 0);
  session.recordInbound(result.sequence);
  assert(session.lastInboundSequence() == 1);

  result = validator.validate(webHello.data(), webHello.size(), 1000, false,
                              session);
  assert(!result.valid);
  assert(result.errorCode == kRateLimitedCode);

  const std::string gatewayHello = hello("gateway", kProtocolVersion, 2);
  result = validator.validate(gatewayHello.data(), gatewayHello.size(), 1000,
                              false, session);
  assert(!result.valid);
  assert(result.errorCode == kUnauthorizedCode);
  result = validator.validate(gatewayHello.data(), gatewayHello.size(), 1000,
                              true, session);
  assert(result.valid);
  assert(result.syncClock);
  assert(result.epochMs == 1795277402100LL);

  const std::string badVersion = hello("web", "2.0.0", 3);
  result = validator.validate(badVersion.data(), badVersion.size(), 1000, false,
                              session);
  assert(!result.valid);
  assert(result.errorCode == kUnsupportedVersionCode);
  assert(std::strcmp(protocolErrorCode(result.errorCode),
                     "UNSUPPORTED_PROTOCOL_VERSION") == 0);

  const char unknown[] =
      "{\"protocolVersion\":\"1.0.0\",\"type\":\"rf.telemetry\","
      "\"timestamp\":\"2026-11-21T16:10:02.100Z\",\"sequence\":4,"
      "\"source\":\"web-dashboard\",\"sessionId\":\"s\",\"payload\":{}}";
  result = validator.validate(unknown, sizeof(unknown) - 1, 1000, false,
                              session);
  assert(!result.valid);
  assert(result.errorCode == kUnknownTypeCode);

  const char malformed[] = "{\"protocolVersion\":NaN}";
  result = validator.validate(malformed, sizeof(malformed) - 1, 1000, false,
                              session);
  assert(!result.valid);
  assert(result.errorCode == kInvalidEnvelopeCode);

  std::puts("test_session: ALL TESTS PASSED");
}
