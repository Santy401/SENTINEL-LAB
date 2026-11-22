#pragma once

#include "realtime/auth.h"
#include "realtime/event_bridge.h"
#include "realtime/phone_motion.h"
#include "realtime/protocol_adapter.h"
#include "realtime/rgb_engine.h"
#include "realtime/time_source.h"
#include "realtime/ws_server.h"

#include <stdint.h>

namespace sentinel {
namespace realtime {

// Owns the P2 lifecycle. Only this class schedules periodic Protocol v1
// envelopes, deduplicates event-engine records, and applies fresh time from
// authenticated gateway hellos.
class RealtimeCore {
 public:
  void begin(uint16_t port, const char* bootId, const AuthGuard& auth);

  void loop(uint32_t nowMs);

  size_t clientCount() const { return server_.clientCount(); }
  uint32_t droppedFrames() const { return server_.droppedFrames(); }
  uint32_t rejectedMessages() const { return server_.rejectedMessages(); }
  uint32_t droppedTelemetry() const { return droppedTelemetry_; }
  uint32_t skippedSimulatedEvents() const { return skippedSimulatedEvents_; }
  bool clockSynced() const { return clock_.synced(); }
  uint32_t freeHeapBytes() const { return freeHeapBytes_; }
  uint32_t minFreeHeapBytes() const { return minFreeHeapBytes_; }
  uint32_t heapSamples() const { return heapSamples_; }
  uint32_t largestFreeBlockBytes() const { return largestFreeBlockBytes_; }
  uint32_t minLoopStackFreeBytes() const { return minLoopStackFreeBytes_; }
  const UtcClock& clock() const { return clock_; }
  const PhoneMotion& phoneMotion() const { return phone_; }
  const RgbEngine& rgb() const { return rgb_; }
  const RealtimeServer& server() const { return server_; }

 private:
  static constexpr uint32_t kRfIntervalMs = 1000;
  static constexpr uint32_t kHeartbeatIntervalMs = 5000;
  static constexpr uint32_t kEventPollIntervalMs = 100;
  static constexpr uint32_t kNtpRetryMs = 5000;
  static constexpr uint32_t kHeapSampleIntervalMs = 15000;

  void armSchedule(uint32_t nowMs);
  static void onMotion(void* context, const InboundMessage& message,
                       uint32_t nowMs);
  void relayMotion(uint32_t nowMs);
  void updateRgb(uint32_t nowMs);
  void sampleHeap(uint32_t nowMs);
  void tryNtpSync(uint32_t nowMs);
  void publishRf(uint32_t nowMs);
  void publishSystem(uint32_t nowMs);
  void publishMotion(uint32_t nowMs);
  void publishEvents(uint32_t nowMs);

  UtcClock clock_;
  ProtocolAdapter adapter_{clock_};
  RealtimeServer server_;
  EventBridge events_;
  SessionState systemSession_;
  char bootId_[17]{};
  uint32_t lastRf_ = 0;
  uint32_t lastHeartbeat_ = 0;
  uint32_t lastSystem_ = 0;
  uint32_t lastMotion_ = 0;
  uint32_t lastEventPoll_ = 0;
  uint32_t lastNtpCheck_ = 0;
  uint32_t lastHeapSample_ = 0;
  uint32_t freeHeapBytes_ = 0;
  uint32_t minFreeHeapBytes_ = 0;
  uint32_t heapSamples_ = 0;
  uint32_t largestFreeBlockBytes_ = 0;
  uint32_t minLoopStackFreeBytes_ = 0;
  // Fixed buffers for periodic envelopes; kept off the loop task stack.
  RfClientView rfClients_[8]{};
  char message_[kProtocolMessageBytes]{};
  bool eventBridgeStarted_ = false;
  bool scheduleArmed_ = false;
  static constexpr uint32_t kMotionRelayMs = 200;
  static constexpr uint32_t kRgbFrameMs = 20;
  PhoneMotion phone_;
  RgbEngine rgb_;
  uint32_t lastRgbFrame_ = 0;
  uint32_t lastRelay_ = 0;
  bool relayPending_ = false;
  // Copy of the latest validated sample for the relay (fixed buffers).
  char relayDeviceId_[65]{};
  char relaySource_[16]{};
  char relayState_[16]{};
  char relaySensorTs_[25]{};
  MotionSampleView relay_;
  uint32_t droppedTelemetry_ = 0;
  uint32_t skippedSimulatedEvents_ = 0;
};

}  // namespace realtime
}  // namespace sentinel
