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

  // Inbound sessionId is owned by the client: the first well-formed value is
  // bound, a different value later in the same connection is unauthorized,
  // and dropping the field after binding is rejected too.
  SessionState fresh;
  envelope = hello("web", kProtocolVersion, 1, "client-run-001", "client-ui");
  result = validator.validate(envelope.data(), envelope.size(), 1000, false,
                              fresh);
  assert(result.valid);
  assert(std::strcmp(fresh.inboundSource(), "client-ui") == 0);
  assert(std::strcmp(fresh.inboundSessionId(), "client-run-001") == 0);

  envelope = hello("web", kProtocolVersion, 2, "client-run-002", "client-ui");
  result = validator.validate(envelope.data(), envelope.size(), 1000, false,
                              fresh);
  assert(!result.valid);
  assert(result.errorCode == kUnauthorizedCode);

  envelope = hello("web", kProtocolVersion, 3, nullptr, "client-ui");
  result = validator.validate(envelope.data(), envelope.size(), 1000, false,
                              fresh);
  assert(!result.valid);
  assert(result.errorCode == kUnauthorizedCode);

  // A reconnect starts a new binding: the client's new sessionId is accepted.
  envelope = hello("web", kProtocolVersion, 1, "client-run-003", "client-ui");
  result = validator.validate(envelope.data(), envelope.size(), 1000, false,
                              reconnected);
  assert(result.valid);
  assert(std::strcmp(reconnected.inboundSessionId(), "client-run-003") == 0);

  // role "ios": read-only without a token, time authority with one.
  SessionState iosAnon;
  iosAnon.begin(1, "boot1234");
  envelope = hello("ios", kProtocolVersion, 1, "ios-run-1", "ios-app");
  result = validator.validate(envelope.data(), envelope.size(), 1000, false,
                              iosAnon);
  assert(result.valid);
  assert(!result.syncClock);
  assert(!result.privileged);

  SessionState iosAuth;
  iosAuth.begin(2, "boot1234");
  result = validator.validate(envelope.data(), envelope.size(), 1000, true,
                              iosAuth);
  assert(result.valid);
  assert(result.syncClock);
  assert(result.privileged);
  iosAuth.markPrivileged();

  // role "web" follows the same rule as "ios".
  SessionState webAnon;
  webAnon.begin(3, "boot1234");
  envelope = hello("web", kProtocolVersion, 1, "web-run-1", "web-dashboard");
  result = validator.validate(envelope.data(), envelope.size(), 1000, false,
                              webAnon);
  assert(result.valid && !result.syncClock && !result.privileged);
  SessionState webAuth;
  webAuth.begin(3, "boot1234");
  result = validator.validate(envelope.data(), envelope.size(), 1000, true,
                              webAuth);
  assert(result.valid && result.syncClock && result.privileged);

  // Authenticated heartbeat from the privileged session re-syncs the clock;
  // the same heartbeat from an unprivileged session never does.
  const char heartbeatTemplate[] =
      "{\"protocolVersion\":\"1.0.0\",\"type\":\"connection.heartbeat\","
      "\"timestamp\":\"2026-11-21T16:10:07.100Z\",\"sequence\":2,"
      "\"source\":\"ios-app\",\"sessionId\":\"ios-run-1\",\"payload\":{"
      "\"uptimeMs\":5000}}";
  result = validator.validate(heartbeatTemplate, sizeof(heartbeatTemplate) - 1,
                              1000, true, iosAuth);
  assert(result.valid);
  assert(result.syncClock);
  result = validator.validate(heartbeatTemplate, sizeof(heartbeatTemplate) - 1,
                              1000, false, iosAnon);
  assert(result.valid);
  assert(!result.syncClock);

  reconnected.begin(3, "boot1234");
  assert(!reconnected.hasInboundSource());
  assert(reconnected.sessionId()[0] != '\0');

  std::puts("test_session: ALL TESTS PASSED");
}
