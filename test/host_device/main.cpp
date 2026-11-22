// Host emulator of the firmware realtime endpoint. It runs the real
// RealtimeServer, ProtocolAdapter, session validator, clock and auth guard on
// a loopback TCP port so the gateway, web and iOS clients can be contract
// tested on Linux. It is NOT hardware evidence: it has no radio, its RF list is
// empty and every frame it emits says so through bootId "host-emulator".
#include "realtime/auth.h"
#include "realtime/protocol_adapter.h"
#include "realtime/time_source.h"
#include "realtime/ws_server.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

using namespace sentinel::realtime;

namespace {

volatile sig_atomic_t gStop = 0;
void onSignal(int) { gStop = 1; }

uint32_t monotonicMs() {
  timespec now{};
  clock_gettime(CLOCK_MONOTONIC, &now);
  return static_cast<uint32_t>(now.tv_sec * 1000ULL + now.tv_nsec / 1000000ULL);
}

}  // namespace

int main(int argc, char** argv) {
  const uint16_t port = argc > 1 ? static_cast<uint16_t>(atoi(argv[1])) : 8181;
  const char* token = getenv("SENTINEL_EMULATOR_TOKEN");
  signal(SIGINT, onSignal);
  signal(SIGTERM, onSignal);

  static UtcClock clock;
  static ProtocolAdapter adapter(clock);
  static const AuthGuard auth(token ? token : "");
  static RealtimeServer server;
  static SessionState system;
  static char message[kProtocolMessageBytes];
  const char bootId[] = "host-emulator";
  system.begin(0, bootId, true);
  server.begin(port, bootId, clock, adapter, auth);
  printf("emulator listening on 127.0.0.1:%u (token %s)\n", port,
         token && *token ? "configured" : "absent");
  fflush(stdout);

  uint32_t lastHeartbeat = monotonicMs();
  uint32_t lastStatus = 0;
  bool wasSynced = false;
  while (!gStop) {
    const uint32_t now = monotonicMs();
    server.loop(now);
    if (clock.synced() && !wasSynced) {
      wasSynced = true;
      printf("clock synced from %s\n", clockSourceName(clock.source()));
      fflush(stdout);
    }
    if (clock.synced() && uint32_t(now - lastHeartbeat) >= 1000) {
      lastHeartbeat = now;
      if (adapter.buildHeartbeat(system.source(), system.sessionId(),
                                 system.nextSequence(), now, bootId, message,
                                 sizeof(message))) {
        server.broadcast(message, strlen(message));
      }
    }
    if (uint32_t(now - lastStatus) >= 1000) {
      lastStatus = now;
      printf("status clients=%u dropped=%u rejected=%u synced=%d\n",
             static_cast<unsigned>(server.clientCount()), server.droppedFrames(),
             server.rejectedMessages(), clock.synced() ? 1 : 0);
      fflush(stdout);
    }
    usleep(2000);
  }
  return 0;
}
