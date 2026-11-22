#include "realtime/ws_server.h"

#include "realtime/handshake.h"

#include <stdio.h>
#include <string.h>

#if defined(ARDUINO)
#include <errno.h>
#include <lwip/sockets.h>
#endif

namespace sentinel {
namespace realtime {
namespace {

constexpr char kBadRequest[] =
    "HTTP/1.1 400 Bad Request\r\nConnection: close\r\n\r\n";
constexpr char kUpgradeRequired[] =
    "HTTP/1.1 426 Upgrade Required\r\n"
    "Connection: close\r\n"
    "Sec-WebSocket-Version: 13\r\n"
    "\r\n";
constexpr char kRequestTimeout[] =
    "HTTP/1.1 408 Request Timeout\r\nConnection: close\r\n\r\n";

// Detects a TCP FIN/RST that arrived without a WebSocket Close frame. The
// Arduino WiFiClient::connected() check uses a zero-length recv() and keeps
// reporting a half-closed socket as connected, which previously held a slot
// until the 60 s idle timeout. Only called when no bytes are pending.
#if defined(ARDUINO)
bool remoteClosed(WiFiClient& client) {
  const int fd = client.fd();
  if (fd < 0) return true;
  uint8_t byte = 0;
  const int result = recv(fd, &byte, 1, MSG_PEEK | MSG_DONTWAIT);
  if (result == 0) return true;
  if (result < 0) {
    return errno != EWOULDBLOCK && errno != EAGAIN && errno != EINTR;
  }
  return false;
}
#else
bool remoteClosed(WiFiClient& client) { return client.remoteClosed(); }
#endif

}  // namespace

void RealtimeServer::begin(uint16_t port, const char* bootId, UtcClock& clock,
                           ProtocolAdapter& adapter, const AuthGuard& auth) {
  bootId_ = bootId && *bootId ? bootId : "boot";
  clock_ = &clock;
  adapter_ = &adapter;
  auth_ = &auth;
  server_.begin(port);
}

size_t RealtimeServer::clientCount() const {
  size_t count = 0;
  for (const Peer& peer : peers_) {
    if (peer.active && peer.handshake) ++count;
  }
  return count;
}

void RealtimeServer::resetPeer(Peer& peer) {
  peer.client.stop();
  peer.client = WiFiClient();
  peer.active = false;
  peer.handshake = false;
  peer.authenticated = false;
  peer.helloSent = false;
  peer.helloPending = false;
  peer.request[0] = '\0';
  peer.requestLength = 0;
  peer.decoder.reset();
  peer.tx.clear();
  peer.session = SessionState();
  peer.acceptedAt = 0;
  peer.lastActivity = 0;
  peer.lastPing = 0;
  peer.rateWindow = 0;
  peer.framesThisWindow = 0;
}

void RealtimeServer::acceptPending(uint32_t nowMs) {
  for (size_t attempt = 0; attempt < kMaxPeers; ++attempt) {
    WiFiClient client = server_.accept();
    if (!client) break;
    size_t slot = kMaxPeers;
    for (size_t i = 0; i < kMaxPeers; ++i) {
      if (!peers_[i].active) {
        slot = i;
        break;
      }
    }
    if (slot == kMaxPeers) {
      client.stop();
      ++dropped_;
      continue;
    }
    Peer& peer = peers_[slot];
    resetPeer(peer);
    peer.client = client;
    peer.active = true;
    peer.acceptedAt = nowMs;
    peer.lastActivity = nowMs;
    peer.lastPing = nowMs;
    peer.rateWindow = nowMs;
  }
}

void RealtimeServer::rejectHandshake(Peer& peer, const char* response,
                                     size_t length) {
  if (peer.client.connected()) peer.client.write(response, length);
  resetPeer(peer);
}

void RealtimeServer::handleHandshake(Peer& peer, size_t slot, uint32_t nowMs) {
  const int available = peer.client.available();
  if (available <= 0) {
    // A socket that never completes the upgrade must not hold one of the four
    // slots: close it after a short, fixed deadline.
    if (!peer.client.connected() || remoteClosed(peer.client)) {
      ++remoteCloses_;
      resetPeer(peer);
    } else if (uint32_t(nowMs - peer.acceptedAt) > kHandshakeTimeoutMs) {
      ++handshakeTimeouts_;
      rejectHandshake(peer, kRequestTimeout, sizeof(kRequestTimeout) - 1);
    }
    return;
  }

  const size_t room = sizeof(peer.request) - peer.requestLength - 1;
  const size_t wanted = room < static_cast<size_t>(available)
                            ? room
                            : static_cast<size_t>(available);
  const int read = peer.client.read(
      reinterpret_cast<uint8_t*>(peer.request + peer.requestLength), wanted);
  if (read <= 0) return;
  peer.requestLength += static_cast<size_t>(read);
  peer.request[peer.requestLength] = '\0';

  const char* terminator = nullptr;
  for (size_t i = 3; i < peer.requestLength; ++i) {
    if (peer.request[i] == '\n' && peer.request[i - 1] == '\r' &&
        peer.request[i - 2] == '\n' && peer.request[i - 3] == '\r') {
      terminator = peer.request + i + 1;
      break;
    }
  }
  if (!terminator) {
    if (peer.requestLength + 1 >= sizeof(peer.request)) {
      ++rejected_;
      rejectHandshake(peer, kBadRequest, sizeof(kBadRequest) - 1);
    } else if (uint32_t(nowMs - peer.acceptedAt) > kHandshakeTimeoutMs) {
      // Slow-drip requests are bounded by the same deadline.
      ++handshakeTimeouts_;
      rejectHandshake(peer, kRequestTimeout, sizeof(kRequestTimeout) - 1);
    }
    return;
  }

  HandshakeRequest request;
  if (!parseHandshake(peer.request,
                      static_cast<size_t>(terminator - peer.request),
                      request)) {
    ++rejected_;
    if (request.status == HandshakeStatus::UpgradeRequired) {
      rejectHandshake(peer, kUpgradeRequired, sizeof(kUpgradeRequired) - 1);
    } else {
      rejectHandshake(peer, kBadRequest, sizeof(kBadRequest) - 1);
    }
    return;
  }

  char accept[32]{};
  if (!websocketAccept(request.key, accept, sizeof(accept))) {
    resetPeer(peer);
    return;
  }

  const int written = snprintf(json_, sizeof(json_),
                               "HTTP/1.1 101 Switching Protocols\r\n"
                               "Upgrade: websocket\r\n"
                               "Connection: Upgrade\r\n"
                               "Sec-WebSocket-Accept: %s\r\n"
                               "Sec-WebSocket-Protocol: sentinel.v1\r\n"
                               "Cache-Control: no-store\r\n"
                               "\r\n",
                               accept);
  if (written <= 0 ||
      peer.client.write(json_, static_cast<size_t>(written)) !=
          static_cast<size_t>(written)) {
    resetPeer(peer);
    return;
  }

  peer.handshake = true;
  peer.authenticated = auth_ && auth_->verifyBearer(
                           request.authorization, strlen(request.authorization));
  peer.session.begin(static_cast<uint8_t>(slot), bootId_);
  peer.lastActivity = nowMs;
  peer.lastPing = nowMs;
  // The request buffer is no longer needed; clear it so a bearer token does
  // not linger in RAM for the lifetime of the session.
  memset(peer.request, 0, sizeof(peer.request));
  peer.requestLength = 0;
  sendHello(peer, nowMs);
}

void RealtimeServer::handleFrames(Peer& peer, uint32_t nowMs) {
  if (!peer.client.connected()) {
    ++remoteCloses_;
    resetPeer(peer);
    return;
  }
  const int available = peer.client.available();
  if (available <= 0) {
    if (remoteClosed(peer.client)) {
      ++remoteCloses_;
      resetPeer(peer);
    }
    return;
  }

  const size_t wanted = static_cast<size_t>(available) < sizeof(rx_)
                            ? static_cast<size_t>(available)
                            : sizeof(rx_);
  const int read = peer.client.read(rx_, wanted);
  if (read <= 0) return;

  peer.lastActivity = nowMs;
  size_t offset = 0;
  while (offset < static_cast<size_t>(read)) {
    size_t payloadLength = 0;
    size_t consumed = 0;
    const WsResult result = peer.decoder.feed(
        rx_ + offset, static_cast<size_t>(read) - offset, payload_,
        sizeof(payload_), payloadLength, consumed);
    offset += consumed;

    switch (result) {
      case WsResult::NeedMore:
        return;
      case WsResult::Text:
        handleText(peer, payload_, payloadLength, nowMs);
        break;
      case WsResult::Ping:
        enqueueFrame(peer, WsOpcode::Pong, payload_, payloadLength);
        break;
      case WsResult::Pong:
        break;
      case WsResult::Close:
        closePeer(peer, WsCloseCode::Normal);
        return;
      case WsResult::TooLarge:
        ++rejected_;
        closePeer(peer, WsCloseCode::TooLarge);
        return;
      case WsResult::Error:
        ++rejected_;
        closePeer(peer, WsCloseCode::ProtocolError);
        return;
    }
    if (!peer.active || !peer.handshake) return;
    peer.decoder.reset();
  }
}

void RealtimeServer::handleText(Peer& peer, const char* payload, size_t length,
                                uint32_t nowMs) {
  if (uint32_t(nowMs - peer.rateWindow) >= kRateWindowMs) {
    peer.rateWindow = nowMs;
    peer.framesThisWindow = 0;
  }
  if (peer.framesThisWindow < 255) ++peer.framesThisWindow;
  if (peer.framesThisWindow > kMaxFramesPerSecond) {
    ++rejected_;
    // Only the first excess frame in a window is answered, so a flood cannot
    // turn into an equal flood of outbound errors.
    if (peer.framesThisWindow == kMaxFramesPerSecond + 1) {
      sendError(peer, nowMs, kRateLimitedCode, "frame rate limit exceeded", "",
                0);
    }
    return;
  }

  const InboundMessage message =
      adapter_ ? InboundValidator().validate(payload, length, nowMs,
                                             peer.authenticated, peer.session)
               : InboundMessage{};
  if (!message.valid) {
    ++rejected_;
    sendError(peer, nowMs, message.errorCode, message.errorText,
              message.source[0] ? message.source : "", message.sequence);
    return;
  }

  peer.session.recordInbound(message.sequence);
  if (message.privileged) peer.session.markPrivileged();
  if (message.syncClock && clock_ &&
      clock_->sync(message.epochMs, nowMs, ClockSource::Peer)) {
    sendHello(peer, nowMs);
  }
  if (strcmp(message.type, "connection.hello") == 0) {
    peer.session.markHello();
    sendHello(peer, nowMs);
  } else if (strcmp(message.type, "connection.goodbye") == 0) {
    if (adapter_ && peer.client.connected() &&
        adapter_->buildGoodbye(peer.session.source(), peer.session.sessionId(),
                               peer.session.nextSequence(), nowMs,
                               "client-request", json_, sizeof(json_))) {
      // Send the acknowledgement before closePeer() stops the client; a
      // queued frame would never be drained after the socket is closed.
      const size_t encoded = encodeFrame(WsOpcode::Text, json_, strlen(json_),
                                         frame_, sizeof(frame_));
      if (encoded && peer.client.write(frame_, encoded) != encoded) {
        ++dropped_;
      }
    }
    closePeer(peer, WsCloseCode::Normal);
  }
}

void RealtimeServer::enqueueFrame(Peer& peer, WsOpcode opcode,
                                  const char* data, size_t length) {
  const size_t encoded =
      encodeFrame(opcode, data, length, frame_, sizeof(frame_));
  if (encoded == 0 ||
      !peer.tx.push(reinterpret_cast<const char*>(frame_), encoded)) {
    ++dropped_;
  }
}

void RealtimeServer::enqueueText(Peer& peer, const char* text, size_t length) {
  if (peer.handshake && peer.client.connected()) {
    enqueueFrame(peer, WsOpcode::Text, text, length);
  }
}

void RealtimeServer::drain(Peer& peer) {
  size_t length = 0;
  for (size_t i = 0; i < kTxDepth; ++i) {
    if (!peer.tx.pop(reinterpret_cast<char*>(frame_), sizeof(frame_), length)) {
      break;
    }
    if (peer.client.write(frame_, length) != length) {
      ++remoteCloses_;
      resetPeer(peer);
      return;
    }
  }
}

void RealtimeServer::keepAlive(Peer& peer, uint32_t nowMs) {
  // Pongs and any other client traffic refresh lastActivity, so a passive
  // client (one that only reads telemetry) stays connected as long as its
  // WebSocket stack answers pings.
  if (uint32_t(nowMs - peer.lastActivity) > kIdleTimeoutMs) {
    ++idleTimeouts_;
    closePeer(peer, WsCloseCode::GoingAway);
    return;
  }
  if (uint32_t(nowMs - peer.lastPing) >= kPingIntervalMs) {
    peer.lastPing = nowMs;
    enqueueFrame(peer, WsOpcode::Ping, nullptr, 0);
  }
}

void RealtimeServer::closePeer(Peer& peer, WsCloseCode code) {
  const uint16_t value = static_cast<uint16_t>(code);
  const char status[2] = {static_cast<char>(value >> 8),
                          static_cast<char>(value & 0xFF)};
  uint8_t frame[8]{};
  const size_t length =
      encodeFrame(WsOpcode::Close, status, sizeof(status), frame, sizeof(frame));
  if (length && peer.client.connected()) {
    peer.client.write(frame, length);
  }
  resetPeer(peer);
}

void RealtimeServer::sendHello(Peer& peer, uint32_t nowMs) {
  if (!adapter_ || peer.helloSent) return;
  if (!clock_ || !clock_->synced()) {
    peer.helloPending = true;
    return;
  }
  const char* const capabilities[] = {"rf.monitor", "motion.ingest",
                                      "laboratory"};
  if (!adapter_->buildHello(peer.session.source(), peer.session.sessionId(),
                            peer.session.nextSequence(), nowMs, bootId_,
                            "firmware", capabilities, 3, json_,
                            sizeof(json_))) {
    return;
  }
  peer.helloPending = false;
  peer.helloSent = true;
  enqueueText(peer, json_, strlen(json_));
}

void RealtimeServer::retryPendingHellos(uint32_t nowMs) {
  if (!clock_ || !clock_->synced()) return;
  for (Peer& peer : peers_) {
    if (peer.active && peer.handshake && peer.helloPending) {
      sendHello(peer, nowMs);
    }
  }
}

void RealtimeServer::sendError(Peer& peer, uint32_t nowMs, uint8_t code,
                               const char* message,
                               const char* relatedSource,
                               uint64_t relatedSequence) {
  if (!adapter_) return;
  if (!adapter_->buildError(peer.session.source(), peer.session.sessionId(),
                            peer.session.nextSequence(), nowMs,
                            protocolErrorCode(code), message, false,
                            relatedSource, relatedSequence, json_,
                            sizeof(json_))) {
    return;
  }
  enqueueText(peer, json_, strlen(json_));
}

void RealtimeServer::broadcast(const char* message, size_t length) {
  if (!message || length == 0 || length > kWsMaxFrameBytes) return;
  const size_t encoded = encodeFrame(WsOpcode::Text, message, length, frame_,
                                     sizeof(frame_));
  if (encoded == 0) return;
  for (Peer& peer : peers_) {
    if (!peer.active || !peer.handshake || !peer.client.connected()) continue;
    if (!peer.tx.push(reinterpret_cast<const char*>(frame_), encoded)) {
      ++dropped_;
    }
  }
}

void RealtimeServer::loop(uint32_t nowMs) {
  acceptPending(nowMs);
  retryPendingHellos(nowMs);
  for (size_t i = 0; i < kMaxPeers; ++i) {
    Peer& peer = peers_[i];
    if (!peer.active) continue;
    if (!peer.handshake) {
      handleHandshake(peer, i, nowMs);
      continue;
    }
    handleFrames(peer, nowMs);
    if (peer.active) keepAlive(peer, nowMs);
    if (peer.active) drain(peer);
  }
}

}  // namespace realtime
}  // namespace sentinel
