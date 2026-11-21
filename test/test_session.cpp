#include "realtime/protocol_adapter.h"
#include "realtime/session.h"

#include <ArduinoJson.h>

#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>

using namespace sentinel::realtime;

namespace {

std::string hello(const char* role, const char* version, uint64_t sequence,
                  const char* sessionId = nullptr,
                  const char* source = "web-dashboard") {
  JsonDocument document;
  JsonObject payload = document["payload"].to<JsonObject>();
  document["protocolVersion"] = kProtocolVersion;
  document["type"] = "connection.hello";
  document["timestamp"] = "2026-11-21T16:10:02.100Z";
  document["sequence"] = sequence;
  document["source"] = source;
  if (sessionId) document["sessionId"] = sessionId;
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
  const std::string webHello =
      hello("web", kProtocolVersion, 1, session.sessionId());
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

  const std::string gatewayHello =
      hello("gateway", kProtocolVersion, 2, session.sessionId());
  result = validator.validate(gatewayHello.data(), gatewayHello.size(), 1000,
                              false, session);
  assert(!result.valid);
  assert(result.errorCode == kUnauthorizedCode);
  result = validator.validate(gatewayHello.data(), gatewayHello.size(), 1000,
                              true, session);
  assert(result.valid);
  assert(result.syncClock);
  assert(result.epochMs == 1795277402100LL);

  const std::string badVersion =
      hello("web", "2.0.0", 3, session.sessionId());
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

  // Inbound session identity: malformed sessionId values are invalid envelopes,
  // mismatches and mid-session source changes are unauthorized.
  const char* assigned = session.sessionId();
  std::string envelope = hello("web", kProtocolVersion, 11, "");
  result = validator.validate(envelope.data(), envelope.size(), 1000, false,
                              session);
  assert(!result.valid);
  assert(result.errorCode == kInvalidEnvelopeCode);

  envelope = hello("web", kProtocolVersion, 12, "bad id!");
  result = validator.validate(envelope.data(), envelope.size(), 1000, false,
                              session);
  assert(!result.valid);
  assert(result.errorCode == kInvalidEnvelopeCode);

  const std::string tooLongId(65, 'a');
  envelope = hello("web", kProtocolVersion, 13, tooLongId.c_str());
  result = validator.validate(envelope.data(), envelope.size(), 1000, false,
                              session);
  assert(!result.valid);
  assert(result.errorCode == kInvalidEnvelopeCode);

  const char numericSessionId[] =
      "{\"protocolVersion\":\"1.0.0\",\"type\":\"connection.hello\","
      "\"timestamp\":\"2026-11-21T16:10:02.100Z\",\"sequence\":14,"
      "\"source\":\"web-dashboard\",\"sessionId\":12345,\"payload\":{"
      "\"role\":\"web\",\"supportedVersion\":\"1.0.0\"}}";
  result = validator.validate(numericSessionId, sizeof(numericSessionId) - 1,
                              1000, false, session);
  assert(!result.valid);
  assert(result.errorCode == kInvalidEnvelopeCode);

  envelope = hello("web", kProtocolVersion, 15, "boot1234-p2-stale");
  result = validator.validate(envelope.data(), envelope.size(), 1000, false,
                              session);
  assert(!result.valid);
  assert(result.errorCode == kUnauthorizedCode);

  envelope = hello("web", kProtocolVersion, 16, assigned, "other-client");
  result = validator.validate(envelope.data(), envelope.size(), 1000, false,
                              session);
  assert(!result.valid);
  assert(result.errorCode == kUnauthorizedCode);
  assert(std::strcmp(session.inboundSource(), "web-dashboard") == 0);

  envelope = hello("web", kProtocolVersion, 17, assigned);
  result = validator.validate(envelope.data(), envelope.size(), 1000, false,
                              session);
  assert(result.valid);
  assert(std::strcmp(result.source, "web-dashboard") == 0);

  // A peer without an assigned session id supplies its own; a stale id from a
  // previous connection is rejected against the reconnected session.
  SessionState fresh;
  envelope = hello("web", kProtocolVersion, 1, "client-run-001", "client-ui");
  result = validator.validate(envelope.data(), envelope.size(), 1000, false,
                              fresh);
  assert(result.valid);
  assert(std::strcmp(fresh.inboundSource(), "client-ui") == 0);

  envelope = hello("web", kProtocolVersion, 1, assigned);
  result = validator.validate(envelope.data(), envelope.size(), 1000, false,
                              reconnected);
  assert(!result.valid);
  assert(result.errorCode == kUnauthorizedCode);

  reconnected.begin(3, "boot1234");
  assert(!reconnected.hasInboundSource());
  assert(reconnected.sessionId()[0] != '\0');

  std::puts("test_session: ALL TESTS PASSED");
}
