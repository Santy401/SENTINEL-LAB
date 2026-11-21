#include "realtime/ws_server.h"

#include "realtime/handshake.h"

#include <stdio.h>
#include <string.h>

namespace sentinel {
namespace realtime {

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
    peer = Peer{};
    peer.client = client;
    peer.active = true;
    peer.lastActivity = nowMs;
    peer.rateWindow = nowMs;
  }
}

void RealtimeServer::handleHandshake(Peer& peer, size_t slot, uint32_t nowMs) {
  const int available = peer.client.available();
  if (available <= 0) return;

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
      static const char kBad[] =
          "HTTP/1.1 400 Bad Request\r\nConnection: close\r\n\r\n";
      peer.client.write(kBad, sizeof(kBad) - 1);
      closePeer(peer, "client-request");
    }
    return;
  }

  HandshakeRequest request;
  if (!parseHandshake(peer.request, static_cast<size_t>(terminator - peer.request),
                      request)) {
    if (request.status == HandshakeStatus::UpgradeRequired) {
      static const char kBadVersion[] =
          "HTTP/1.1 426 Upgrade Required\r\n"
          "Connection: close\r\n"
          "Sec-WebSocket-Version: 13\r\n"
          "\r\n";
      peer.client.write(kBadVersion, sizeof(kBadVersion) - 1);
    } else {
      static const char kBad[] =
          "HTTP/1.1 400 Bad Request\r\nConnection: close\r\n\r\n";
      peer.client.write(kBad, sizeof(kBad) - 1);
    }
    closePeer(peer, "client-request");
    return;
  }

  char accept[32]{};
  if (!websocketAccept(request.key, accept, sizeof(accept))) {
    closePeer(peer, "internal-error");
    return;
  }

  char response[256]{};
  snprintf(response, sizeof(response),
           "HTTP/1.1 101 Switching Protocols\r\n"
           "Upgrade: websocket\r\n"
           "Connection: Upgrade\r\n"
           "Sec-WebSocket-Accept: %s\r\n"
           "Sec-WebSocket-Protocol: sentinel.v1\r\n"
           "Cache-Control: no-store\r\n"
           "\r\n",
           accept);
  if (peer.client.write(response, strlen(response)) != strlen(response)) {
    closePeer(peer, "client-request");
    return;
  }

  peer.handshake = true;
  peer.authenticated = auth_ && auth_->verifyBearer(
                           request.authorization, strlen(request.authorization));
  peer.session.begin(static_cast<uint8_t>(slot), bootId_);
  peer.lastActivity = nowMs;
  sendHello(peer, nowMs);
}

void RealtimeServer::handleFrames(Peer& peer, size_t slot, uint32_t nowMs) {
  (void)slot;
  if (!peer.client.connected()) {
    closePeer(peer, "client-request");
    return;
  }
  const int available = peer.client.available();
  if (available <= 0) {
    if (uint32_t(nowMs - peer.lastActivity) > kIdleTimeoutMs) {
      closePeer(peer, "timeout");
    }
    return;
  }

  constexpr size_t kReadBudget = 512;
  uint8_t buffer[kReadBudget]{};
  char payload[kWsMaxFrameBytes]{};
  const size_t wanted = static_cast<size_t>(available) < kReadBudget
                            ? static_cast<size_t>(available)
                            : kReadBudget;
  const int read = peer.client.read(buffer, wanted);
  if (read <= 0) return;

  peer.lastActivity = nowMs;
  size_t offset = 0;
  while (offset < static_cast<size_t>(read)) {
    size_t payloadLength = 0;
    size_t consumed = 0;
    const WsResult result = peer.decoder.feed(
        buffer + offset, static_cast<size_t>(read) - offset, payload,
        sizeof(payload), payloadLength, consumed);
    offset += consumed;

    switch (result) {
      case WsResult::NeedMore:
        return;
      case WsResult::Text:
        handleText(peer, payload, payloadLength, nowMs);
        break;
      case WsResult::Ping:
        enqueueFrame(peer, WsOpcode::Pong, payload, payloadLength);
        break;
      case WsResult::Pong:
        break;
      case WsResult::Close:
        closePeer(peer, "client-request");
        return;
      case WsResult::Error:
      case WsResult::TooLarge:
        ++rejected_;
        closePeer(peer, "client-request");
        return;
    }
    peer.decoder.reset();
    if (!peer.active || !peer.handshake) return;
  }
}

void RealtimeServer::handleText(Peer& peer, char* payload, size_t length,
                                uint32_t nowMs) {
  if (uint32_t(nowMs - peer.rateWindow) >= kRateWindowMs) {
    peer.rateWindow = nowMs;
    peer.framesThisWindow = 0;
  }
  if (++peer.framesThisWindow > kMaxFramesPerSecond) {
    ++rejected_;
    sendError(peer, nowMs, kRateLimitedCode, "frame rate limit exceeded", "",
              0);
    return;
  }

  const InboundMessage message = adapter_ ? InboundValidator().validate(
      payload, length, nowMs, peer.authenticated, peer.session)
                                          : InboundMessage{};
  if (!message.valid) {
    ++rejected_;
    sendError(peer, nowMs, message.errorCode, message.errorText,
              message.source[0] ? message.source : "", message.sequence);
    return;
  }

  peer.session.recordInbound(message.sequence);
  if (message.syncClock && clock_) {
    clock_->sync(message.epochMs, nowMs);
    sendHello(peer, nowMs);
  }
  if (strcmp(message.type, "connection.hello") == 0) {
    peer.session.markHello();
    if (AuthGuard::privilegedRole(message.role)) peer.session.markPrivileged();
    sendHello(peer, nowMs);
  } else if (strcmp(message.type, "connection.goodbye") == 0) {
    char goodbye[kProtocolMessageBytes]{};
    if (adapter_ && adapter_->buildGoodbye(
                        peer.session.source(), peer.session.sessionId(),
                        peer.session.nextSequence(), nowMs, "client-request",
                        goodbye, sizeof(goodbye)) &&
        peer.client.connected()) {
      // Send the acknowledgement before closePeer() stops the client; a
      // queued frame would never be drained after the socket is closed.
      uint8_t frame[kWsEncodedFrameBytes]{};
      const size_t length = strlen(goodbye);
      const size_t encoded =
          encodeFrame(WsOpcode::Text, goodbye, length, frame, sizeof(frame));
      if (encoded && peer.client.write(frame, encoded) != encoded) ++dropped_;
    }
    closePeer(peer, "client-request");
  }
}

void RealtimeServer::enqueueFrame(Peer& peer, WsOpcode opcode,
                                  const char* data, size_t length) {
  uint8_t frame[kWsEncodedFrameBytes]{};
  const size_t encoded =
      encodeFrame(opcode, data, length, frame, sizeof(frame));
  if (encoded == 0 || !peer.tx.push(reinterpret_cast<char*>(frame), encoded)) {
    ++dropped_;
  }
}

void RealtimeServer::enqueue(Peer& peer, const char* data, size_t length) {
  enqueueFrame(peer, WsOpcode::Text, data, length);
}

void RealtimeServer::enqueueText(Peer& peer, const char* text, size_t length) {
  if (peer.handshake && peer.client.connected()) {
    enqueue(peer, text, length);
  }
}

void RealtimeServer::drain(Peer& peer, uint32_t nowMs) {
  char message[kWsEncodedFrameBytes]{};
  size_t length = 0;
  for (size_t i = 0; i < 2; ++i) {
    if (!peer.tx.pop(message, sizeof(message), length)) break;
    if (peer.client.write(message, length) != length) {
      closePeer(peer, "client-request");
      return;
    }
  }
  if (uint32_t(nowMs - peer.lastActivity) > kIdleTimeoutMs) {
    closePeer(peer, "timeout");
  }
}

void RealtimeServer::closePeer(Peer& peer, const char* reason) {
  (void)reason;
  uint8_t frame[4]{};
  const size_t length = encodeFrame(WsOpcode::Close, nullptr, 0, frame,
                                    sizeof(frame));
  if (length && peer.client.connected()) {
    peer.client.write(frame, length);
  }
  peer.client.stop();
  peer = Peer{};
}

void RealtimeServer::sendHello(Peer& peer, uint32_t nowMs) {
  if (!adapter_ || peer.helloSent) return;
  if (!clock_ || !clock_->synced()) {
    peer.helloPending = true;
    return;
  }
  const char* const capabilities[] = {"rf.monitor", "motion.ingest",
                                      "laboratory"};
  char hello[kProtocolMessageBytes]{};
  if (!adapter_->buildHello(peer.session.source(), peer.session.sessionId(),
                            peer.session.nextSequence(), nowMs, bootId_,
                            "firmware", capabilities, 3, hello,
                            sizeof(hello))) {
    return;
  }
  peer.helloPending = false;
  peer.helloSent = true;
  enqueueText(peer, hello, strlen(hello));
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
  char error[kProtocolMessageBytes]{};
  if (!adapter_->buildError(peer.session.source(), peer.session.sessionId(),
                            peer.session.nextSequence(), nowMs,
                            protocolErrorCode(code), message, false,
                            relatedSource, relatedSequence, error,
                            sizeof(error))) {
    return;
  }
  enqueueText(peer, error, strlen(error));
}

void RealtimeServer::broadcast(const char* message, size_t length) {
  if (!message || length == 0 || length > kWsMaxFrameBytes) return;
  uint8_t frame[kWsEncodedFrameBytes]{};
  const size_t encoded = encodeFrame(WsOpcode::Text, message, length, frame,
                                     sizeof(frame));
  if (encoded == 0) return;
  for (Peer& peer : peers_) {
    if (!peer.handshake || !peer.client.connected()) continue;
    if (!peer.tx.push(reinterpret_cast<char*>(frame), encoded)) {
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
    } else {
      handleFrames(peer, i, nowMs);
      if (peer.active && peer.handshake) drain(peer, nowMs);
    }
  }
}

}  // namespace realtime
}  // namespace sentinel
