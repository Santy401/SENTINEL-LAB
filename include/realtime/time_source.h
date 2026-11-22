#pragma once

#include <stddef.h>
#include <stdint.h>

namespace sentinel {
namespace realtime {

// Origin of the last accepted wall-clock synchronization. "peer" means the
// timestamp of an authenticated Protocol v1 hello/heartbeat.
enum class ClockSource : uint8_t { None, Ntp, Peer };

const char* clockSourceName(ClockSource source);

// Converts the ESP32 monotonic clock into the UTC ISO 8601 form required by
// Protocol v1. The firmware never invents wall-clock time: a gateway must
// synchronize the clock first, either from an authorized local NTP server or
// from the timestamp of an authenticated Protocol v1 envelope.
class UtcClock {
 public:
  bool synced() const { return synced_; }
  // Ignores implausible gateway/NTP epochs outside 2020-01-01..2100-01-01.
  // Returns true when the sample was accepted.
  bool sync(int64_t epochMs, uint32_t monotonicMs,
            ClockSource source = ClockSource::Peer);
  void clear();
  ClockSource source() const { return synced_ ? source_ : ClockSource::None; }
  // Monotonic time of the last accepted sync; meaningful only when synced().
  uint32_t syncedAtMs() const { return monotonicAtSync_; }

  // Writes YYYY-MM-DDTHH:MM:SS.mmmZ. Returns false when unsynchronized or
  // when the output capacity is too small.
  bool format(uint32_t monotonicMs, char* output, size_t capacity) const;

  // Strict parser for the Protocol v1 date-time subset.
  static bool parse(const char* text, size_t length, int64_t& epochMs);

 private:
  int64_t epochAtSync_ = 0;
  uint32_t monotonicAtSync_ = 0;
  ClockSource source_ = ClockSource::None;
  bool synced_ = false;
};

}  // namespace realtime
}  // namespace sentinel
