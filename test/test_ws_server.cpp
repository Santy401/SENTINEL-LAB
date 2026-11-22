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

std::string iosHello(uint64_t sequence, const char* timestamp) {
  JsonDocument document;
  document["protocolVersion"] = kProtocolVersion;
  document["type"] = "connection.hello";
  document["timestamp"] = timestamp;
  document["sequence"] = sequence;
  document["source"] = "ios-app";
  document["sessionId"] = "ios-session-1";
  JsonObject payload = document["payload"].to<JsonObject>();
  payload["deviceId"] = "iphone-12";
  payload["role"] = "ios";
  payload["supportedVersion"] = kProtocolVersion;
  payload["capabilities"].to<JsonArray>().add("telemetry.view");
  std::string text;
  serializeJson(document, text);
  return text;
}

std::string iosHeartbeat(uint64_t sequence) {
  JsonDocument document;
  document["protocolVersion"] = kProtocolVersion;
  document["type"] = "connection.heartbeat";
  document["timestamp"] = "2026-11-21T16:10:05.000Z";
  document["sequence"] = sequence;
  document["source"] = "ios-app";
  document["sessionId"] = "ios-session-1";
  document["payload"]["uptimeMs"] = 1000;
  std::string text;
  serializeJson(document, text);
  return text;
}

size_t countOf(const std::string& haystack, const std::string& needle) {
  size_t count = 0;
  for (size_t at = haystack.find(needle); at != std::string::npos;
       at = haystack.find(needle, at + needle.size())) {
    ++count;
  }
  return count;
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

  // Handshake deadline: a socket that never sends the upgrade request is
  // answered with 408 and its slot is released after kHandshakeTimeoutMs.
  {
    RealtimeServer s;
    s.begin(81, "boottimeout", clock, adapter, auth);
    gPendingWiFiClients.clear();
    WiFiClient silent = WiFiClient::create();
    gPendingWiFiClients.push_back(silent);
    s.loop(10000);
    assert(silent.connected());
    s.loop(10000 + RealtimeServer::kHandshakeTimeoutMs);
    assert(silent.connected());
    s.loop(10001 + RealtimeServer::kHandshakeTimeoutMs);
    assert(!silent.connected());
    assert(silent.sent().find("408 Request Timeout") != std::string::npos);
    assert(s.handshakeTimeouts() == 1);

    // Slow-drip partial request is bounded by the same deadline.
    WiFiClient drip = WiFiClient::create();
    drip.push("GET /realtime HTTP/1.1\r\nHost: x\r\n");
    gPendingWiFiClients.push_back(drip);
    s.loop(20000);
    drip.push("Upgrade: websocket\r\n");
    s.loop(20000 + RealtimeServer::kHandshakeTimeoutMs + 1);
    assert(!drip.connected());
    assert(s.handshakeTimeouts() == 2);

    // A client that disconnects before upgrading frees its slot at once.
    WiFiClient gone = WiFiClient::create();
    gPendingWiFiClients.push_back(gone);
    s.loop(30000);
    gone.finish();
    s.loop(30001);
    assert(s.remoteCloses() == 1);

    // All four slots are free again for real clients.
    std::vector<WiFiClient> fresh;
    for (int i = 0; i < 4; ++i) fresh.push_back(connectClient(false));
    s.loop(30002);
    s.loop(30003);
    assert(s.clientCount() == 4);
  }

  // TCP FIN without a Close frame releases the slot on the next loop instead
  // of after the 60 s idle timeout.
  {
    RealtimeServer s;
    s.begin(81, "bootfin", clock, adapter, auth);
    gPendingWiFiClients.clear();
    WiFiClient half = connectClient(false);
    finishHandshake(s, half, 40000);
    assert(s.clientCount() == 1);
    half.finish();
    s.loop(40002);
    assert(s.clientCount() == 0);
    assert(s.remoteCloses() == 1);
  }

  // Keepalive: the server pings every kPingIntervalMs; a passive client whose
  // stack answers with Pong stays connected past the idle timeout, and one
  // that never answers is closed with status 1001 once idle.
  {
    RealtimeServer s;
    s.begin(81, "bootping", clock, adapter, auth);
    gPendingWiFiClients.clear();
    WiFiClient passive = connectClient(false);
    WiFiClient mute = connectClient(false);
    finishHandshake(s, passive, 50000);
    s.loop(50002);
    assert(s.clientCount() == 2);
    passive.clearSent();
    mute.clearSent();
    const uint32_t firstPing = 50001 + RealtimeServer::kPingIntervalMs;
    s.loop(firstPing);
    const std::string ping("\x89\x00", 2);
    assert(passive.sent().find(ping) != std::string::npos);
    assert(mute.sent().find(ping) != std::string::npos);

    uint32_t now = firstPing;
    for (int i = 0; i < 4; ++i) {
      passive.push(maskedFrame(WsOpcode::Pong, ""));
      now += RealtimeServer::kPingIntervalMs;
      s.loop(now);
    }
    // now is 80 s after the handshake: the passive peer survived, the mute one
    // exceeded the 60 s idle limit.
    assert(passive.connected());
    assert(!mute.connected());
    const std::string goingAway("\x88\x02\x03\xe9", 4);
    assert(mute.sent().find(goingAway) != std::string::npos);
    assert(s.idleTimeouts() == 1);
    assert(s.clientCount() == 1);
  }

  // The iPhone app as time authority: an authenticated role "ios" hello syncs
  // the clock; without the bearer token it does not.
  {
    UtcClock iosClock;
    ProtocolAdapter iosAdapter(iosClock);
    RealtimeServer s;
    s.begin(81, "bootios", iosClock, iosAdapter, auth);
    gPendingWiFiClients.clear();

    WiFiClient anonymous = connectClient(false);
    finishHandshake(s, anonymous, 60000);
    anonymous.push(maskedFrame(WsOpcode::Text,
                               iosHello(1, "2026-11-21T16:10:02.100Z")));
    s.loop(60002);
    assert(!iosClock.synced());
    assert(anonymous.sent().find("connection.hello") == std::string::npos);

    WiFiClient phone = connectClient(true);
    s.loop(60003);
    s.loop(60004);
    phone.push(maskedFrame(WsOpcode::Text,
                           iosHello(1, "2026-11-21T16:10:02.100Z")));
    s.loop(60005);
    assert(iosClock.synced());
    assert(iosClock.source() == ClockSource::Peer);
    s.loop(60006);
    assert(phone.sent().find("connection.hello") != std::string::npos);
    // The peer that connected earlier also receives its pending hello.
    assert(anonymous.sent().find("connection.hello") != std::string::npos);

    // Its heartbeat re-syncs the clock at the new monotonic reference.
    phone.push(maskedFrame(WsOpcode::Text, iosHeartbeat(2)));
    s.loop(70000);
    assert(iosClock.syncedAtMs() == 70000);

    // A client-owned sessionId cannot change mid-connection.
    phone.clearSent();
    std::string switched = iosHeartbeat(3);
    const std::string from = "ios-session-1";
    switched.replace(switched.find(from), from.size(), "ios-session-2");
    phone.push(maskedFrame(WsOpcode::Text, switched));
    s.loop(70001);
    s.loop(70002);
    assert(phone.sent().find("UNAUTHORIZED") != std::string::npos);
  }

  // Rate limiting answers a flood with a single RATE_LIMITED error per window.
  {
    RealtimeServer s;
    s.begin(81, "bootrate", clock, adapter, auth);
    gPendingWiFiClients.clear();
    WiFiClient flood = connectClient(false);
    finishHandshake(s, flood, 80000);
    flood.clearSent();
    for (uint64_t seq = 1; seq <= 14; ++seq) {
      flood.push(maskedFrame(WsOpcode::Text, iosHeartbeat(seq)));
    }
    for (int i = 0; i < 12; ++i) s.loop(80002);
    assert(countOf(flood.sent(), "RATE_LIMITED") == 1);
    assert(flood.connected());
  }

  // Hardware regression (2026-11-21): the sync hello plus three periodic
  // envelopes in the same loop overflowed a two-frame queue. With kTxDepth = 3
  // the hello drains first and the burst of three fits without drops.
  {
    UtcClock burstClock;
    ProtocolAdapter burstAdapter(burstClock);
    RealtimeServer s;
    s.begin(81, "bootburst", burstClock, burstAdapter, auth);
    gPendingWiFiClients.clear();
    WiFiClient gateway = connectClient(true);
    finishHandshake(s, gateway, 90000);
    gateway.push(maskedFrame(WsOpcode::Text, gatewayHello(1)));
    s.loop(90002);
    assert(burstClock.synced());
    const std::string frame = "{\"type\":\"x\"}";
    s.broadcast(frame.data(), frame.size());
    s.broadcast(frame.data(), frame.size());
    s.broadcast(frame.data(), frame.size());
    s.loop(90003);
    assert(s.droppedFrames() == 0);
    assert(countOf(gateway.sent(), frame) == 3);
  }

  std::puts("test_ws_server: ALL TESTS PASSED");
}
