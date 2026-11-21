#include "realtime/auth.h"
#include "realtime/bounded_queue.h"
#include "realtime/event_bridge.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>

using namespace sentinel::realtime;

int main() {
  BoundedQueue<2, 8> queue;
  char output[8]{};
  size_t length = 0;
  assert(queue.empty());
  assert(queue.push("one", 3));
  assert(queue.push("two", 3));
  assert(!queue.push("three", 5));
  assert(queue.dropped() == 1);
  assert(queue.pop(output, sizeof(output), length));
  assert(std::string(output, length) == "one");
  assert(queue.pop(output, sizeof(output), length));
  assert(std::string(output, length) == "two");
  assert(queue.empty());

  EventBridge bridge;
  bridge.begin(10);
  assert(!bridge.consume(10, false));
  assert(!bridge.consume(11, true));
  assert(bridge.consume(11, false));
  assert(!bridge.consume(11, false));
  assert(bridge.consume(12, false));
  assert(bridge.lastSequence() == 12);

  const AuthGuard guard("local-token-123");
  assert(guard.verifyBearer("Bearer local-token-123", 22));
  assert(!guard.verifyBearer("Bearer wrong-token", 18));
  assert(guard.verifyBearer("bearer local-token-123", 22));
  assert(guard.verifyBearer("BEARER local-token-123", 22));
  assert(!AuthGuard("").verifyBearer("Bearer local-token-123", 22));
  assert(AuthGuard::privilegedRole("gateway"));
  assert(!AuthGuard::privilegedRole("web"));

  std::puts("test_realtime_limits: ALL TESTS PASSED");
}
