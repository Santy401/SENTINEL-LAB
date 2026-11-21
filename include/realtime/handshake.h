#pragma once

#include <stddef.h>

namespace sentinel {
namespace realtime {

struct HandshakeRequest {
  char key[128]{};
  char authorization[160]{};
  bool upgrade = false;
  bool connection = false;
  bool valid = false;
};

bool parseHandshake(const char* request, size_t length,
                    HandshakeRequest& result);

// Computes the RFC 6455 Sec-WebSocket-Accept value. Returns false when the
// output buffer is too small.
bool websocketAccept(const char* key, char* output, size_t capacity);

}  // namespace realtime
}  // namespace sentinel
