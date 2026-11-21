#include "realtime/frame_codec.h"
#include "realtime/protocol_adapter.h"
#include "realtime/ws_server.h"

#include <ArduinoJson.h>

#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace sentinel::realtime;

namespace {

std::vector<uint8_t> maskedFrame(WsOpcode opcode, const std::string& text) {
  std::vector<uint8_t> frame;
  frame.push_back(0x80 | static_cast<uint8_t>(opcode));
  if (text.size() <= 125) {
    frame.push_back(0x80 | static_cast<uint8_t>(text.size()));
  } else {
    frame.push_back(0x80 | 126);
    frame.push_back(static_cast<uint8_t>(text.size() >> 8));
    frame.push_back(static_cast<uint8_t>(text.size() & 0xFF));
  }
  const uint8_t mask[4] = {0x11, 0x22, 0x33, 0x44};
  for (uint8_t byte : mask) frame.push_back(byte);
  for (size_t i = 0; i < text.size(); ++i) {
    frame.push_back(static_cast<uint8_t>(text[i]) ^ mask[i & 3]);
  }
  return frame;
}

struct HandshakeOptions {
  bool withAuth = false;
  bool hasVersion = true;
  bool hasProtocol = true;
  std::string version = "13";
  std::string protocol = "sentinel.v1";
};

// Realistic RFC 6455 client request, including the version and subprotocol
// required by this endpoint.
std::string buildHandshake(const HandshakeOptions& options) {
  std::string request =
      "GET /realtime HTTP/1.1\r\n"
      "Host: sentinel.local\r\n"
      "Origin: http://sentinel.local\r\n"
      "Upgrade: websocket\r\n"
      "Connection: keep-alive, Upgrade\r\n"
      "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n";
  if (options.hasVersion) {
    request += "Sec-WebSocket-Version: " + options.version + "\r\n";
  }
  if (options.hasProtocol) {
    request += "Sec-WebSocket-Protocol: " + options.protocol + "\r\n";
  }
  if (options.withAuth) {
    request += "Authorization: Bearer local-token-123\r\n";
  }
  request += "\r\n";
  return request;
}

WiFiClient connectClientWith(const HandshakeOptions& options) {
  WiFiClient client = WiFiClient::create();
  client.push(buildHandshake(options));
  gPendingWiFiClients.push_back(client);
  return client;
}

WiFiClient connectClient(bool withAuth) {
  HandshakeOptions options;
  options.withAuth = withAuth;
  return connectClientWith(options);
}

std::string gatewayHello(uint64_t sequence) {
  JsonDocument document;
  document["protocolVersion"] = kProtocolVersion;
  document["type"] = "connection.hello";
  document["timestamp"] = "2026-11-21T16:10:02.100Z";
  document["sequence"] = sequence;
  document["source"] = "gateway-lab-01";
  JsonObject payload = document["payload"].to<JsonObject>();
  payload["deviceId"] = "gateway-01";
  payload["role"] = "gateway";
  payload["supportedVersion"] = kProtocolVersion;
  payload["capabilities"].to<JsonArray>().add("telemetry");
  std::string text;
  serializeJson(document, text);
  return text;
}

std::string goodbye(uint64_t sequence) {
  JsonDocument document;
  document["protocolVersion"] = kProtocolVersion;
  document["type"] = "connection.goodbye";
  document["timestamp"] = "2026-11-21T16:10:02.100Z";
  document["sequence"] = sequence;
  document["source"] = "gateway-lab-01";
  document["payload"]["reason"] = "client-request";
  std::string text;
  serializeJson(document, text);
  return text;
}

void finishHandshake(RealtimeServer& server, WiFiClient& client,
                     uint32_t nowMs) {
  server.loop(nowMs);
  server.loop(nowMs + 1);
  assert(client.sent().find("101 Switching Protocols") != std::string::npos);
}

void expectHandshakeRejected(RealtimeServer& server,
                             const HandshakeOptions& options, uint32_t nowMs,
                             const char* statusLine,
                             const char* requiredHeader = nullptr) {
  gPendingWiFiClients.clear();
  WiFiClient client = connectClientWith(options);
  server.loop(nowMs);
  const std::string sent = client.sent();
  assert(sent.find(statusLine) != std::string::npos);
  assert(sent.find("101 Switching Protocols") == std::string::npos);
  if (requiredHeader) {
    assert(sent.find(requiredHeader) != std::string::npos);
  }
  assert(!client.connected());
  assert(server.clientCount() == 0);
}

}  // namespace

int main() {
  UtcClock clock;
  clock.sync(1795277402100LL, 1000);
  ProtocolAdapter adapter(clock);
  const AuthGuard auth("local-token-123");
  RealtimeServer server;
  server.begin(81, "boot1234", clock, adapter, auth);

  // Unsupported versions require an explicit 426 with the supported version.
  HandshakeOptions oldVersion;
  oldVersion.version = "12";
  expectHandshakeRejected(server, oldVersion, 900,
                          "HTTP/1.1 426 Upgrade Required",
                          "Sec-WebSocket-Version: 13");

  // Missing version, missing subprotocol, and a different subprotocol are
  // plain bad requests: the server must not answer with 101.
  HandshakeOptions missingVersion;
  missingVersion.hasVersion = false;
  expectHandshakeRejected(server, missingVersion, 901,
                          "HTTP/1.1 400 Bad Request");
  HandshakeOptions missingProtocol;
  missingProtocol.hasProtocol = false;
  expectHandshakeRejected(server, missingProtocol, 902,
                          "HTTP/1.1 400 Bad Request");
  HandshakeOptions wrongProtocol;
  wrongProtocol.protocol = "sentinel.v2";
  expectHandshakeRejected(server, wrongProtocol, 903,
                          "HTTP/1.1 400 Bad Request");

  // A standard client may offer several versions/subprotocols in one list.
  HandshakeOptions listHeaders;
  listHeaders.version = "8, 13";
  listHeaders.protocol = "chat, sentinel.v1";
  gPendingWiFiClients.clear();
  WiFiClient listClient = connectClientWith(listHeaders);
  finishHandshake(server, listClient, 910);
  assert(listClient.sent().find("Sec-WebSocket-Protocol: sentinel.v1") !=
         std::string::npos);
  listClient.push(maskedFrame(WsOpcode::Close, ""));
  server.loop(911);
  assert(server.clientCount() == 0);

  // Full lifecycle: authenticated handshake, privileged hello, ordered goodbye.
  gPendingWiFiClients.clear();
  WiFiClient client = connectClient(true);
  finishHandshake(server, client, 1000);
  assert(server.clientCount() == 1);
  assert(client.sent().find("connection.hello") != std::string::npos);
  client.clearSent();

  client.push(maskedFrame(WsOpcode::Text, gatewayHello(1)));
  server.loop(1002);
  assert(client.sent().find("\"type\":\"error\"") == std::string::npos);
  client.clearSent();

  client.push(maskedFrame(WsOpcode::Text, goodbye(2)));
  server.loop(1003);
  assert(client.sent().find("connection.goodbye") != std::string::npos);
  assert(!client.connected());
  assert(server.clientCount() == 0);

  // Privileged role without token is rejected and cannot sync the clock.
  gPendingWiFiClients.clear();
  WiFiClient unauthenticated = connectClient(false);
  finishHandshake(server, unauthenticated, 2000);
  unauthenticated.clearSent();
  unauthenticated.push(maskedFrame(WsOpcode::Text, gatewayHello(1)));
  server.loop(2002);
  assert(unauthenticated.sent().find("UNAUTHORIZED") != std::string::npos);
  unauthenticated.stop();
  server.loop(2003);

  // Malformed protocol text returns an error; an oversized frame closes.
  gPendingWiFiClients.clear();
  WiFiClient malformed = connectClient(false);
  finishHandshake(server, malformed, 3000);
  malformed.clearSent();
  malformed.push(maskedFrame(WsOpcode::Text, "not-json"));
  server.loop(3002);
  assert(malformed.sent().find("INVALID_ENVELOPE") != std::string::npos);
  assert(malformed.connected());
  malformed.clearSent();
  malformed.push(maskedFrame(WsOpcode::Text, std::string(1025, 'a')));
  server.loop(3003);
  assert(!malformed.connected());
  assert(server.clientCount() == 0);

  // Peer cap: the fifth local client is refused while four remain connected.
  gPendingWiFiClients.clear();
  std::vector<WiFiClient> clients;
  for (int i = 0; i < 5; ++i) clients.push_back(connectClient(false));
  server.loop(4000);
  server.loop(4001);
  server.loop(4002);
  assert(server.clientCount() == 4);
  assert(!clients[4].connected());
  for (int i = 0; i < 4; ++i) {
    clients[i].push(maskedFrame(WsOpcode::Close, ""));
  }
  server.loop(4003);
  assert(server.clientCount() == 0);
  assert(server.rejectedMessages() > 0);

  // A peer that connects before clock sync receives its hello once time is
  // available, even if a different peer supplied the synchronization.
  {
    UtcClock pendingClock;
    ProtocolAdapter pendingAdapter(pendingClock);
    RealtimeServer pendingServer;
    pendingServer.begin(81, "bootpending", pendingClock, pendingAdapter, auth);
    gPendingWiFiClients.clear();
    WiFiClient waiting = connectClient(false);
    finishHandshake(pendingServer, waiting, 5000);
    assert(waiting.sent().find("connection.hello") == std::string::npos);
    waiting.clearSent();
    pendingClock.sync(1795277402100LL, 5001);
    pendingServer.loop(5002);
    assert(waiting.sent().find("connection.hello") != std::string::npos);
  }

  std::puts("test_ws_server: ALL TESTS PASSED");
}
