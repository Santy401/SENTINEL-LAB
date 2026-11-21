#pragma once

#include <stddef.h>

namespace sentinel {
namespace realtime {

// Bearer-token guard for privileged WebSocket sessions. An empty configured
// token means privileged operations are disabled rather than open to everyone.
class AuthGuard {
 public:
  explicit AuthGuard(const char* configuredToken) : token_(configuredToken) {}

  bool verifyBearer(const char* header, size_t length) const;

  // Only a gateway session may synchronize the firmware clock. Telemetry
  // remains readable by every local laboratory client.
  static bool privilegedRole(const char* role);

 private:
  const char* token_;
};

}  // namespace realtime
}  // namespace sentinel
