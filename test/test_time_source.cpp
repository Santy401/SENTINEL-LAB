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
