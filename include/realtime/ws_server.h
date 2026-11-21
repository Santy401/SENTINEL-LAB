#pragma once

#include <WiFi.h>

#include <stddef.h>
#include <stdint.h>

#include "realtime/auth.h"
#include "realtime/bounded_queue.h"
#include "realtime/frame_codec.h"
#include "realtime/protocol_adapter.h"
#include "realtime/session.h"
#include "realtime/time_source.h"

namespace sentinel {
namespace realtime {

// Minimal RFC 6455 endpoint for the local laboratory AP. It uses a bounded
// number of peers, rejects fragmented or oversized frames, and never builds
// JSON inside a network callback. All Protocol v1 envelopes are prepared by
// ProtocolAdapter and queued as complete frames.
class RealtimeServer {
 public:
  void begin(uint16_t port, const char* bootId, UtcClock& clock,
             ProtocolAdapter& adapter, const AuthGuard& auth);

  void loop(uint32_t nowMs);
  void broadcast(const char* message, size_t length);

  size_t clientCount() const;
  uint32_t droppedFrames() const { return dropped_; }
  uint32_t rejectedMessages() const { return rejected_; }

 private:
  static constexpr size_t kMaxPeers = 4;
  static constexpr uint32_t kIdleTimeoutMs = 60000;
  static constexpr uint32_t kRateWindowMs = 1000;
  static constexpr uint8_t kMaxFramesPerSecond = 8;

  struct Peer {
    WiFiClient client;
    bool active = false;
    bool handshake = false;
    bool authenticated = false;
    bool helloSent = false;
    bool helloPending = false;
    char request[kWsMaxFrameBytes]{};
    size_t requestLength = 0;
    WsDecoder decoder;
    BoundedQueue<2, kWsEncodedFrameBytes> tx;
    SessionState session;
    uint32_t lastActivity = 0;
    uint32_t rateWindow = 0;
    uint8_t framesThisWindow = 0;
  };

  void acceptPending(uint32_t nowMs);
  void handleHandshake(Peer& peer, size_t slot, uint32_t nowMs);
  void handleFrames(Peer& peer, size_t slot, uint32_t nowMs);
  void handleText(Peer& peer, char* payload, size_t length, uint32_t nowMs);
  void drain(Peer& peer, uint32_t nowMs);
  void closePeer(Peer& peer, const char* reason);
  void enqueueFrame(Peer& peer, WsOpcode opcode, const char* data,
                    size_t length);
  void enqueue(Peer& peer, const char* data, size_t length);
  void enqueueText(Peer& peer, const char* text, size_t length);
  void sendHello(Peer& peer, uint32_t nowMs);
  void retryPendingHellos(uint32_t nowMs);
  void sendError(Peer& peer, uint32_t nowMs, uint8_t code,
                 const char* message, const char* relatedSource,
                 uint64_t relatedSequence);

  WiFiServer server_{81};
  Peer peers_[kMaxPeers];
  const char* bootId_ = "boot";
  UtcClock* clock_ = nullptr;
  ProtocolAdapter* adapter_ = nullptr;
  const AuthGuard* auth_ = nullptr;
  uint32_t dropped_ = 0;
  uint32_t rejected_ = 0;
};

}  // namespace realtime
}  // namespace sentinel
