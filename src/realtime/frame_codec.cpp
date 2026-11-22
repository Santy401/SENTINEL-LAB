#include "realtime/frame_codec.h"

#include <string.h>

namespace sentinel {
namespace realtime {

void WsDecoder::reset() {
  headerLength_ = 0;
  headerNeeded_ = 2;
  payloadLength_ = 0;
  payloadReceived_ = 0;
  opcode_ = static_cast<uint8_t>(WsOpcode::Invalid);
  state_ = State::Header;
  tooLarge_ = false;
}

WsResult WsDecoder::fail(WsResult result) {
  state_ = State::Failed;
  return result;
}

bool WsDecoder::parseHeader() {
  if (headerLength_ < 2) return true;
  const uint8_t first = header_[0];
  const uint8_t second = header_[1];
  const bool fin = (first & 0x80) != 0;
  const bool reserved = (first & 0x70) != 0;
  const bool masked = (second & 0x80) != 0;
  opcode_ = first & 0x0F;

  if (!fin || reserved || !masked) return false;
  if (opcode_ != static_cast<uint8_t>(WsOpcode::Text) &&
      opcode_ != static_cast<uint8_t>(WsOpcode::Close) &&
      opcode_ != static_cast<uint8_t>(WsOpcode::Ping) &&
      opcode_ != static_cast<uint8_t>(WsOpcode::Pong)) {
    return false;
  }

  uint64_t length = second & 0x7F;
  size_t extended = 0;
  if (length == 126) {
    extended = 2;
  } else if (length == 127) {
    extended = 8;
  }
  headerNeeded_ = 2 + extended + 4;
  if (headerLength_ < 2 + extended) return true;

  if (extended == 2) {
    length = (static_cast<uint64_t>(header_[2]) << 8) | header_[3];
  } else if (extended == 8) {
    length = 0;
    for (size_t i = 0; i < 8; ++i) {
      length = (length << 8) | header_[2 + i];
    }
  }

  const bool control = opcode_ == static_cast<uint8_t>(WsOpcode::Close) ||
                       opcode_ == static_cast<uint8_t>(WsOpcode::Ping) ||
                       opcode_ == static_cast<uint8_t>(WsOpcode::Pong);
  if (control && length > kWsMaxControlBytes) return false;
  if (length > kWsMaxFrameBytes) {
    tooLarge_ = true;
    return false;
  }
  if (headerLength_ < headerNeeded_) {
    payloadLength_ = static_cast<size_t>(length);
    return true;
  }

  payloadLength_ = static_cast<size_t>(length);
  const size_t maskOffset = 2 + extended;
  for (size_t i = 0; i < 4; ++i) mask_[i] = header_[maskOffset + i];
  state_ = payloadLength_ == 0 ? State::Done : State::Payload;
  return true;
}

WsResult WsDecoder::feed(const uint8_t* data, size_t length, char* payload,
                         size_t payloadCapacity, size_t& payloadLength) {
  size_t consumed = 0;
  return feed(data, length, payload, payloadCapacity, payloadLength, consumed);
}

WsResult WsDecoder::feed(const uint8_t* data, size_t length, char* payload,
                         size_t payloadCapacity, size_t& payloadLength,
                         size_t& consumed) {
  payloadLength = 0;
  consumed = 0;
  if (!data && length) return fail(WsResult::Error);
  if (state_ == State::Failed) {
    return tooLarge_ ? WsResult::TooLarge : WsResult::Error;
  }
  if (state_ == State::Done) return fail(WsResult::Error);
  if (state_ == State::Payload && (!payload || payloadCapacity < payloadLength_)) {
    return fail(WsResult::TooLarge);
  }

  if (state_ == State::Header) {
    while (consumed < length && headerLength_ < headerNeeded_) {
      header_[headerLength_++] = data[consumed++];
      if (!parseHeader()) {
        return fail(tooLarge_ ? WsResult::TooLarge : WsResult::Error);
      }
    }
    if (headerLength_ < headerNeeded_) return WsResult::NeedMore;
  }

  while (consumed < length && payloadReceived_ < payloadLength_) {
    const size_t index = payloadReceived_++;
    payload[index] = static_cast<char>(data[consumed++] ^ mask_[index & 3]);
  }
  if (payloadReceived_ < payloadLength_) return WsResult::NeedMore;

  payloadLength = payloadLength_;
  state_ = State::Done;
  switch (opcode_) {
    case static_cast<uint8_t>(WsOpcode::Text):
      return WsResult::Text;
    case static_cast<uint8_t>(WsOpcode::Ping):
      return WsResult::Ping;
    case static_cast<uint8_t>(WsOpcode::Pong):
      return WsResult::Pong;
    case static_cast<uint8_t>(WsOpcode::Close):
      return WsResult::Close;
    default:
      return fail(WsResult::Error);
  }
}

size_t encodeFrame(WsOpcode opcode, const char* payload, size_t length,
                   uint8_t* output, size_t outputCapacity) {
  if (!output || (!payload && length)) return 0;
  if (length > kWsMaxFrameBytes) return 0;

  size_t header = 2;
  if (length <= kWsMaxControlBytes) {
    // Fits in the 7-bit length field.
  } else if (length <= 0xFFFF) {
    header = 4;
  } else {
    header = 10;
  }
  if (header + length > outputCapacity) return 0;

  output[0] = static_cast<uint8_t>(0x80 | static_cast<uint8_t>(opcode));
  if (length <= kWsMaxControlBytes) {
    output[1] = static_cast<uint8_t>(length);
  } else if (length <= 0xFFFF) {
    output[1] = 126;
    output[2] = static_cast<uint8_t>(length >> 8);
    output[3] = static_cast<uint8_t>(length & 0xFF);
  } else {
    output[1] = 127;
    for (size_t i = 0; i < 8; ++i) {
      output[2 + i] = static_cast<uint8_t>(length >> (56 - 8 * i));
    }
  }
  if (length) {
    memcpy(output + header, payload, length);
  }
  return header + length;
}

}  // namespace realtime
}  // namespace sentinel
