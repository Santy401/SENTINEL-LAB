#pragma once

#include <stddef.h>

namespace sentinel {
namespace realtime {

// Reason reported when a handshake is rejected. A peer that asked for a
// different WebSocket version must be told which version this endpoint speaks.
enum class HandshakeStatus {
  Ok,
  BadRequest,
  UpgradeRequired,
};

struct HandshakeRequest {
  char key[128]{};
  char authorization[160]{};
  // Token offered as the "sentinel.auth.<token>" subprotocol (browsers).
  char protocolToken[129]{};
  bool upgrade = false;
  bool connection = false;
  bool versionPresent = false;
  bool version13 = false;
  bool subprotocol = false;
  HandshakeStatus status = HandshakeStatus::BadRequest;
  bool valid = false;
};

bool parseHandshake(const char* request, size_t length,
                    HandshakeRequest& result);

// Computes the RFC 6455 Sec-WebSocket-Accept value. Returns false when the
// output buffer is too small.
bool websocketAccept(const char* key, char* output, size_t capacity);

}  // namespace realtime
}  // namespace sentinel
