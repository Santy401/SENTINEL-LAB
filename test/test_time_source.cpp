#include "realtime/time_source.h"

#include <cassert>
#include <cstdio>
#include <cstring>

using namespace sentinel::realtime;

int main() {
  UtcClock clock;
  char buffer[32]{};
  assert(!clock.format(0, buffer, sizeof(buffer)));

  clock.sync(1795277402100LL, 1000);
  assert(clock.synced());
  assert(clock.format(1000, buffer, sizeof(buffer)));
  assert(std::strcmp(buffer, "2026-11-21T16:10:02.100Z") == 0);
  assert(clock.format(1999, buffer, sizeof(buffer)));
  assert(std::strcmp(buffer, "2026-11-21T16:10:03.099Z") == 0);
  assert(clock.format(60000, buffer, sizeof(buffer)));
  assert(std::strcmp(buffer, "2026-11-21T16:11:01.100Z") == 0);

  // millis() wrap must advance the clock by the short elapsed time, not jump
  // backwards by almost 2^32 ms.
  UtcClock wrapClock;
  const uint32_t nearWrap = 0xFFFFFFF0u;
  wrapClock.sync(1795277402100LL, nearWrap);
  assert(wrapClock.format(nearWrap + 5u, buffer, sizeof(buffer)));
  assert(std::strcmp(buffer, "2026-11-21T16:10:02.105Z") == 0);
  assert(wrapClock.format(10u, buffer, sizeof(buffer)));
  assert(std::strcmp(buffer, "2026-11-21T16:10:02.126Z") == 0);

  // Gateway/NTP epochs outside 2020..2100 are rejected; a rejected sync must
  // not replace a previously accepted reference time.
  constexpr int64_t kMinSyncMs = 1577836800000LL;  // 2020-01-01T00:00:00Z
  constexpr int64_t kMaxSyncMs = 4102444800000LL;  // 2100-01-01T00:00:00Z
  UtcClock bounded;
  bounded.sync(kMinSyncMs - 1, 0);
  assert(!bounded.synced());
  assert(!bounded.format(0, buffer, sizeof(buffer)));
  bounded.sync(kMinSyncMs, 0);
  assert(bounded.synced());
  bounded.sync(kMaxSyncMs + 1, 0);
  assert(bounded.format(0, buffer, sizeof(buffer)));
  assert(std::strcmp(buffer, "2020-01-01T00:00:00.000Z") == 0);

  UtcClock upperBound;
  upperBound.sync(kMaxSyncMs + 1, 0);
  assert(!upperBound.synced());
  upperBound.sync(kMaxSyncMs, 0);
  assert(upperBound.synced());
  assert(upperBound.format(0, buffer, sizeof(buffer)));
  assert(std::strcmp(buffer, "2100-01-01T00:00:00.000Z") == 0);

  int64_t epoch = 0;
  assert(UtcClock::parse("1970-01-01T00:00:00.000Z", 24, epoch));
  assert(epoch == 0);
  assert(UtcClock::parse("2026-11-21T16:10:02.100Z", 24, epoch));
  assert(epoch == 1795277402100LL);
  assert(UtcClock::parse("2026-11-21T16:10:02Z", 20, epoch));
  assert(epoch == 1795277402000LL);
  assert(UtcClock::parse("2026-11-21T16:10:02.1Z", 22, epoch));
  assert(epoch == 1795277402100LL);
  assert(!UtcClock::parse("2026-02-30T16:10:02Z", 20, epoch));
  assert(!UtcClock::parse("2026-11-21T16:10:02+00:00", 25, epoch));
  assert(!UtcClock::parse("2026-11-21T16:10:02.1234Z", 25, epoch));
  assert(!UtcClock::parse("1969-12-31T23:59:59Z", 20, epoch));

  clock.clear();
  assert(!clock.synced());
  assert(!clock.format(1000, buffer, sizeof(buffer)));

  std::puts("test_time_source: ALL TESTS PASSED");
}
