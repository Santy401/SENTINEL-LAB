#include "realtime/handshake.h"

#include <cassert>
#include <cstdio>
#include <cstring>

using namespace sentinel::realtime;

int main() {
  const char request[] =
      "GET /realtime HTTP/1.1\r\n"
      "Host: sentinel.local\r\n"
      "Upgrade: websocket\r\n"
      "Connection: Upgrade\r\n"
      "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
      "Authorization: Bearer local-token\r\n"
      "\r\n";
  HandshakeRequest parsed;
  assert(parseHandshake(request, sizeof(request) - 1, parsed));
  assert(std::strcmp(parsed.key, "dGhlIHNhbXBsZSBub25jZQ==") == 0);
  assert(std::strcmp(parsed.authorization, "Bearer local-token") == 0);

  char accept[32]{};
  assert(websocketAccept(parsed.key, accept, sizeof(accept)));
  assert(std::strcmp(accept, "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=") == 0);

  const char wrongPath[] =
      "GET /admin HTTP/1.1\r\n"
      "Upgrade: websocket\r\n"
      "Connection: Upgrade\r\n"
      "Sec-WebSocket-Key: abc\r\n\r\n";
  assert(!parseHandshake(wrongPath, sizeof(wrongPath) - 1, parsed));

  const char missingKey[] =
      "GET /realtime HTTP/1.1\r\n"
      "Upgrade: websocket\r\n"
      "Connection: Upgrade\r\n\r\n";
  assert(!parseHandshake(missingKey, sizeof(missingKey) - 1, parsed));

  std::puts("test_handshake: ALL TESTS PASSED");
}
