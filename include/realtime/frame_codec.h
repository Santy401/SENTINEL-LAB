#pragma once

#include <stddef.h>
#include <stdint.h>

namespace sentinel {
namespace realtime {

// RFC 6455 frame limits used by the firmware WebSocket endpoint. Frames are
// capped well below the 16 KiB protocol envelope limit so a single client
// cannot force a large allocation on the ESP32.
constexpr size_t kWsMaxFrameBytes = 1024;
constexpr size_t kWsMaxControlBytes = 125;
constexpr size_t kWsEncodedFrameBytes = kWsMaxFrameBytes + 10;

enum class WsOpcode : uint8_t {
  Continuation = 0x0,
  Text = 0x1,
  Binary = 0x2,
  Close = 0x8,
  Ping = 0x9,
  Pong = 0xA,
  Invalid = 0xFF
};

enum class WsResult : uint8_t {
  NeedMore,
  Text,
  Ping,
  Pong,
  Close,
  Error,
  TooLarge
};

// Incremental decoder for client-to-server frames. Server frames are generated
// by encodeFrame() and are never masked. Fragmentation is rejected: the
// firmware only accepts complete protocol envelopes in a single text frame.
class WsDecoder {
 public:
  void reset();
  WsResult feed(const uint8_t* data, size_t length, char* payload,
                size_t payloadCapacity, size_t& payloadLength);
  WsResult feed(const uint8_t* data, size_t length, char* payload,
                size_t payloadCapacity, size_t& payloadLength,
                size_t& consumed);

 private:
  enum class State : uint8_t { Header, Payload, Done, Failed };

  bool parseHeader();
  WsResult fail(WsResult result);

  uint8_t header_[14]{};
  size_t headerLength_ = 0;
  size_t headerNeeded_ = 2;
  size_t payloadLength_ = 0;
  size_t payloadReceived_ = 0;
  uint8_t mask_[4]{};
  uint8_t opcode_ = static_cast<uint8_t>(WsOpcode::Invalid);
  State state_ = State::Header;
  bool tooLarge_ = false;
};

// Encodes one unmasked server frame. Returns the number of bytes written, or
// zero when the output buffer is too small.
size_t encodeFrame(WsOpcode opcode, const char* payload, size_t length,
                   uint8_t* output, size_t outputCapacity);

}  // namespace realtime
}  // namespace sentinel
