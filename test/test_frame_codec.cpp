#include "realtime/frame_codec.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace sentinel::realtime;

namespace {

std::vector<uint8_t> maskedFrame(WsOpcode opcode, const std::string& text,
                                 bool fin = true, bool masked = true) {
  std::vector<uint8_t> frame;
  frame.push_back(static_cast<uint8_t>((fin ? 0x80 : 0x00) |
                                       static_cast<uint8_t>(opcode)));
  if (text.size() <= 125) {
    frame.push_back(static_cast<uint8_t>((masked ? 0x80 : 0x00) | text.size()));
  } else {
    frame.push_back(static_cast<uint8_t>((masked ? 0x80 : 0x00) | 126));
    frame.push_back(static_cast<uint8_t>(text.size() >> 8));
    frame.push_back(static_cast<uint8_t>(text.size() & 0xFF));
  }
  if (masked) {
    const uint8_t mask[4] = {0x11, 0x22, 0x33, 0x44};
    for (uint8_t byte : mask) frame.push_back(byte);
    for (size_t i = 0; i < text.size(); ++i) {
      frame.push_back(static_cast<uint8_t>(text[i]) ^ mask[i & 3]);
    }
  } else {
    for (char byte : text) frame.push_back(static_cast<uint8_t>(byte));
  }
  return frame;
}

WsResult feedAll(WsDecoder& decoder, const std::vector<uint8_t>& frame,
                 char* out, size_t capacity, size_t& length) {
  WsResult result = WsResult::NeedMore;
  for (size_t i = 0; i < frame.size(); ++i) {
    result = decoder.feed(frame.data() + i, 1, out, capacity, length);
  }
  return result;
}

}  // namespace

int main() {
  char payload[kWsMaxFrameBytes]{};
  size_t length = 0;

  WsDecoder text;
  const auto frame = maskedFrame(WsOpcode::Text, "hello");
  assert(feedAll(text, frame, payload, sizeof(payload), length) == WsResult::Text);
  assert(length == 5);
  assert(std::string(payload, length) == "hello");

  WsDecoder split;
  length = 0;
  assert(split.feed(frame.data(), 3, payload, sizeof(payload), length) ==
         WsResult::NeedMore);
  assert(split.feed(frame.data() + 3, frame.size() - 3, payload, sizeof(payload),
                    length) == WsResult::Text);
  assert(std::string(payload, length) == "hello");

  WsDecoder ping;
  assert(feedAll(ping, maskedFrame(WsOpcode::Ping, "ping"), payload,
                 sizeof(payload), length) == WsResult::Ping);
  assert(std::string(payload, length) == "ping");

  WsDecoder close;
  assert(feedAll(close, maskedFrame(WsOpcode::Close, ""), payload,
                 sizeof(payload), length) == WsResult::Close);

  WsDecoder fragmented;
  assert(feedAll(fragmented, maskedFrame(WsOpcode::Text, "x", false), payload,
                 sizeof(payload), length) == WsResult::Error);

  WsDecoder unmasked;
  assert(feedAll(unmasked, maskedFrame(WsOpcode::Text, "x", true, false),
                 payload, sizeof(payload), length) == WsResult::Error);

  WsDecoder binary;
  assert(feedAll(binary, maskedFrame(WsOpcode::Binary, "x"), payload,
                 sizeof(payload), length) == WsResult::Error);

  WsDecoder oversized;
  const auto big = maskedFrame(WsOpcode::Text, std::string(kWsMaxFrameBytes + 1, 'a'));
  assert(feedAll(oversized, big, payload, sizeof(payload), length) ==
         WsResult::TooLarge);

  WsDecoder pipelined;
  const auto first = maskedFrame(WsOpcode::Text, "one");
  const auto second = maskedFrame(WsOpcode::Ping, "two");
  std::vector<uint8_t> combined = first;
  combined.insert(combined.end(), second.begin(), second.end());
  size_t consumed = 0;
  assert(pipelined.feed(combined.data(), combined.size(), payload,
                        sizeof(payload), length, consumed) == WsResult::Text);
  assert(consumed == first.size());
  assert(std::string(payload, length) == "one");
  pipelined.reset();
  assert(pipelined.feed(combined.data() + consumed, combined.size() - consumed,
                        payload, sizeof(payload), length, consumed) ==
         WsResult::Ping);
  assert(std::string(payload, length) == "two");

  uint8_t encoded[64]{};
  const std::string reply = "pong";
  const size_t written =
      encodeFrame(WsOpcode::Pong, reply.data(), reply.size(), encoded,
                  sizeof(encoded));
  assert(written == 6);
  assert(encoded[0] == 0x8A);
  assert(encoded[1] == 4);
  assert(std::string(reinterpret_cast<char*>(encoded + 2), 4) == reply);
  assert(encodeFrame(WsOpcode::Text, reply.data(), reply.size(), encoded, 5) == 0);

  std::puts("test_frame_codec: ALL TESTS PASSED");
}
