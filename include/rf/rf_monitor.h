#pragma once
#include <stdint.h>
#include "rf/ring.h"
#include "rf/distance_estimator.h"
namespace sentinel { namespace rf {
struct SignalSample { uint32_t timeMs = 0; int rssi = 0; };
struct Client {
  uint8_t mac[6]{}; // Internal only; never serialized.
  uint32_t id = 0, lastSeenMs = 0;
  bool connected = false, rssiValid = false;
  int rssi = 0;
  Ring<SignalSample, 32> history;
  RssiFilter filter;
};
struct AssociationEvent { uint32_t timeMs, clientId; bool connected; };
class RfMonitor {
 public:
  void update(uint32_t now);
  const Client* clients() const { return clients_; }
  const Ring<AssociationEvent, 32>& events() const { return events_; }
  bool available() const { return available_; }
  uint8_t channel() const { return channel_; }
  uint8_t count() const { return count_; }
 private:
  Client clients_[8]{};
  Ring<AssociationEvent, 32> events_;
  uint32_t lastPoll_ = 0, nextId_ = 1;
  uint8_t channel_ = 0, count_ = 0;
  bool available_ = false;
};
void beginRf();
void updateRf(uint32_t now);
void observeMotion(int state, bool simulated, uint32_t now);
} }
class WebServer;
namespace sentinel { namespace rf { void registerRoutes(WebServer& server); } }
