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

// RFC 6455 close status codes used by the firmware.
enum class WsCloseCode : uint16_t {
  Normal = 1000,
  GoingAway = 1001,
  ProtocolError = 1002,
  PolicyViolation = 1008,
  TooLarge = 1009,
  InternalError = 1011,
};

// Minimal RFC 6455 endpoint for the local laboratory AP. It uses a bounded
// number of peers, rejects fragmented or oversized frames, and never builds
// JSON inside a network callback. All Protocol v1 envelopes are prepared by
// ProtocolAdapter and queued as complete frames.
//
// Memory: every buffer is a member with a fixed size. Nothing large is placed
// on the Arduino loop task stack (8 KiB), and peers are reset in place instead
// of being assigned from a multi-kilobyte temporary.
// Receives validated phone motion samples (authenticated sessions only).
using MotionSink = void (*)(void* context, const InboundMessage& message,
                            uint32_t nowMs);

class RealtimeServer {
 public:
  static constexpr size_t kMaxPeers = 4;
  static constexpr uint32_t kHandshakeTimeoutMs = 5000;
  static constexpr uint32_t kIdleTimeoutMs = 60000;
  static constexpr uint32_t kPingIntervalMs = 20000;
  static constexpr uint32_t kRateWindowMs = 1000;
  static constexpr uint8_t kMaxFramesPerSecond = 8;
  // Outbound frames buffered per peer. Three covers the worst periodic burst
  // (hello + telemetry + status) observed on hardware without growing heap.
  static constexpr size_t kTxDepth = 3;

  void begin(uint16_t port, const char* bootId, UtcClock& clock,
             ProtocolAdapter& adapter, const AuthGuard& auth);

  void loop(uint32_t nowMs);
  void setMotionSink(MotionSink sink, void* context) {
    motionSink_ = sink;
    motionContext_ = context;
  }
  void broadcast(const char* message, size_t length);

  size_t clientCount() const;
  uint32_t droppedFrames() const { return dropped_; }
  uint32_t rejectedMessages() const { return rejected_; }
  uint32_t handshakeTimeouts() const { return handshakeTimeouts_; }
  uint32_t idleTimeouts() const { return idleTimeouts_; }
  uint32_t remoteCloses() const { return remoteCloses_; }

 private:
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
    BoundedQueue<kTxDepth, kWsEncodedFrameBytes> tx;
    SessionState session;
    uint32_t acceptedAt = 0;
    uint32_t lastActivity = 0;
    uint32_t lastPing = 0;
    uint32_t rateWindow = 0;
    uint8_t framesThisWindow = 0;
  };

  void acceptPending(uint32_t nowMs);
  void handleHandshake(Peer& peer, size_t slot, uint32_t nowMs);
  void handleFrames(Peer& peer, uint32_t nowMs);
  void handleText(Peer& peer, const char* payload, size_t length,
                  uint32_t nowMs);
  void drain(Peer& peer);
  void keepAlive(Peer& peer, uint32_t nowMs);
  void rejectHandshake(Peer& peer, const char* response, size_t length);
  void closePeer(Peer& peer, WsCloseCode code);
  void resetPeer(Peer& peer);
  void enqueueFrame(Peer& peer, WsOpcode opcode, const char* data,
                    size_t length);
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
  uint32_t handshakeTimeouts_ = 0;
  uint32_t idleTimeouts_ = 0;
  uint32_t remoteCloses_ = 0;
  MotionSink motionSink_ = nullptr;
  void* motionContext_ = nullptr;

  // Scratch buffers. The loop is single-threaded, and the nesting is fixed:
  // rx_/payload_ (inbound) -> json_ (envelope) -> frame_ (encoded frame).
  uint8_t rx_[512]{};
  char payload_[kWsMaxFrameBytes]{};
  char json_[kProtocolMessageBytes]{};
  uint8_t frame_[kWsEncodedFrameBytes]{};
};

}  // namespace realtime
}  // namespace sentinel
