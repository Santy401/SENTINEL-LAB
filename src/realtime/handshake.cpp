#include "realtime/handshake.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

namespace sentinel {
namespace realtime {
namespace {

constexpr char kGuid[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
constexpr char kSubprotocol[] = "sentinel.v1";

char asciiLower(char value) {
  return value >= 'A' && value <= 'Z'
             ? static_cast<char>(value - 'A' + 'a')
             : value;
}

bool startsWithIgnoreCase(const char* text, size_t length, const char* prefix) {
  const size_t prefixLength = strlen(prefix);
  if (length < prefixLength) return false;
  for (size_t i = 0; i < prefixLength; ++i) {
    if (asciiLower(text[i]) != asciiLower(prefix[i])) return false;
  }
  return true;
}

// The path is case-sensitive; only the method and the HTTP version may differ
// in case. Comparing the whole line also rejects prefixes such as
// "/realtime HTTP/1.10" and alternate paths.
bool isRealtimeRequestLine(const char* text, size_t length) {
  return startsWithIgnoreCase(text, length, "GET /realtime HTTP/1.1") &&
         length == strlen("GET /realtime HTTP/1.1") &&
         memcmp(text + strlen("GET "), "/realtime", strlen("/realtime")) == 0;
}

const char* trim(const char* text, size_t& length) {
  while (length && (text[0] == ' ' || text[0] == '\t' ||
                    text[0] == '\r' || text[0] == '\n')) {
    ++text;
    --length;
  }
  while (length && (text[length - 1] == ' ' || text[length - 1] == '\t' ||
                    text[length - 1] == '\r' || text[length - 1] == '\n')) {
    --length;
  }
  return text;
}

// Checks a comma-separated RFC token list for one exact token. Tokens such as
// "websocket-evil" or "upgradefoo" must not satisfy "websocket"/"upgrade".
bool hasToken(const char* value, size_t length, const char* token,
              bool ignoreCase) {
  while (length) {
    while (length && (*value == ' ' || *value == '\t')) {
      ++value;
      --length;
    }
    size_t tokenLength = 0;
    while (tokenLength < length && value[tokenLength] != ',' &&
           value[tokenLength] != ' ' && value[tokenLength] != '\t') {
      ++tokenLength;
    }
    if (tokenLength == strlen(token)) {
      bool match = true;
      for (size_t i = 0; i < tokenLength; ++i) {
        const char a = ignoreCase ? asciiLower(value[i]) : value[i];
        const char b = ignoreCase ? asciiLower(token[i]) : token[i];
        if (a != b) {
          match = false;
          break;
        }
      }
      if (match) return true;
    }
    while (length && *value != ',') {
      ++value;
      --length;
    }
    if (!length) break;
    ++value;
    --length;
  }
  return false;
}

int base64Index(char value) {
  if (value >= 'A' && value <= 'Z') return value - 'A';
  if (value >= 'a' && value <= 'z') return value - 'a' + 26;
  if (value >= '0' && value <= '9') return value - '0' + 52;
  if (value == '+') return 62;
  if (value == '/') return 63;
  return -1;
}

// A WebSocket key encodes exactly 16 bytes: 24 canonical base64 characters,
// the last two of them padding, and zero unused bits before that padding.
bool isValidWebSocketKey(const char* value, size_t length) {
  if (length != 24 || value[22] != '=' || value[23] != '=') return false;
  for (size_t i = 0; i < 22; ++i) {
    const int index = base64Index(value[i]);
    if (index < 0) return false;
    if (i == 21 && (index & 0x0F) != 0) return false;
  }
  return true;
}

uint32_t rotateLeft(uint32_t value, unsigned bits) {
  return (value << bits) | (value >> (32 - bits));
}

void sha1Block(uint32_t state[5], const uint8_t block[64]) {
  uint32_t words[80]{};
  for (size_t i = 0; i < 16; ++i) {
    words[i] = (static_cast<uint32_t>(block[i * 4]) << 24) |
               (static_cast<uint32_t>(block[i * 4 + 1]) << 16) |
               (static_cast<uint32_t>(block[i * 4 + 2]) << 8) |
               static_cast<uint32_t>(block[i * 4 + 3]);
  }
  for (size_t i = 16; i < 80; ++i) {
    words[i] = rotateLeft(words[i - 3] ^ words[i - 8] ^ words[i - 14] ^
                          words[i - 16], 1);
  }

  uint32_t a = state[0], b = state[1], c = state[2], d = state[3], e = state[4];
  for (size_t i = 0; i < 80; ++i) {
    uint32_t f = 0, k = 0;
    if (i < 20) {
      f = (b & c) | ((~b) & d);
      k = 0x5A827999;
    } else if (i < 40) {
      f = b ^ c ^ d;
      k = 0x6ED9EBA1;
    } else if (i < 60) {
      f = (b & c) | (b & d) | (c & d);
      k = 0x8F1BBCDC;
    } else {
      f = b ^ c ^ d;
      k = 0xCA62C1D6;
    }
    const uint32_t temporary =
        rotateLeft(a, 5) + f + e + k + words[i];
    e = d;
    d = c;
    c = rotateLeft(b, 30);
    b = a;
    a = temporary;
  }
  state[0] += a;
  state[1] += b;
  state[2] += c;
  state[3] += d;
  state[4] += e;
}

void sha1(const uint8_t* data, size_t length, uint8_t digest[20]) {
  uint32_t state[5] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476,
                       0xC3D2E1F0};
  size_t offset = 0;
  while (length - offset >= 64) {
    sha1Block(state, data + offset);
    offset += 64;
  }

  uint8_t tail[128]{};
  const size_t remaining = length - offset;
  for (size_t i = 0; i < remaining; ++i) tail[i] = data[offset + i];
  tail[remaining] = 0x80;
  size_t padded = remaining + 1;
  while (padded % 64 != 56) tail[padded++] = 0;
  const uint64_t bits = static_cast<uint64_t>(length) * 8;
  for (size_t i = 0; i < 8; ++i) {
    tail[padded + i] = static_cast<uint8_t>(bits >> (56 - 8 * i));
  }
  sha1Block(state, tail);
  sha1Block(state, tail + 64);

  for (size_t i = 0; i < 5; ++i) {
    digest[i * 4] = static_cast<uint8_t>(state[i] >> 24);
    digest[i * 4 + 1] = static_cast<uint8_t>(state[i] >> 16);
    digest[i * 4 + 2] = static_cast<uint8_t>(state[i] >> 8);
    digest[i * 4 + 3] = static_cast<uint8_t>(state[i]);
  }
}

void base64(const uint8_t* data, size_t length, char* output) {
  static const char alphabet[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  size_t out = 0;
  for (size_t i = 0; i < length; i += 3) {
    const uint32_t group = (static_cast<uint32_t>(data[i]) << 16) |
                           (i + 1 < length
                                ? static_cast<uint32_t>(data[i + 1]) << 8
                                : 0) |
                           (i + 2 < length
                                ? static_cast<uint32_t>(data[i + 2])
                                : 0);
    output[out++] = alphabet[(group >> 18) & 0x3F];
    output[out++] = alphabet[(group >> 12) & 0x3F];
    output[out++] = i + 1 < length ? alphabet[(group >> 6) & 0x3F] : '=';
    output[out++] = i + 2 < length ? alphabet[group & 0x3F] : '=';
  }
  output[out] = '\0';
}

}  // namespace

bool parseHandshake(const char* request, size_t length,
                    HandshakeRequest& result) {
  result = HandshakeRequest{};
  if (!request || length < 16) return false;
  const char* end = request + length;
  const char* line = request;
  bool requestLineSeen = false;

  while (line < end) {
    const char* lineEnd = line;
    while (lineEnd < end && *lineEnd != '\n') ++lineEnd;
    size_t lineLength = static_cast<size_t>(lineEnd - line);
    if (lineLength && line[lineLength - 1] == '\r') --lineLength;

    if (!requestLineSeen) {
      if (!isRealtimeRequestLine(line, lineLength)) return false;
      requestLineSeen = true;
    } else if (startsWithIgnoreCase(line, lineLength, "Upgrade:")) {
      size_t valueLength = lineLength - strlen("Upgrade:");
      const char* value = trim(line + strlen("Upgrade:"), valueLength);
      if (hasToken(value, valueLength, "websocket", true)) {
        result.upgrade = true;
      }
    } else if (startsWithIgnoreCase(line, lineLength, "Connection:")) {
      size_t valueLength = lineLength - strlen("Connection:");
      const char* value = trim(line + strlen("Connection:"), valueLength);
      if (hasToken(value, valueLength, "upgrade", true)) {
        result.connection = true;
      }
    } else if (startsWithIgnoreCase(line, lineLength,
                                    "Sec-WebSocket-Version:")) {
      constexpr char kHeader[] = "Sec-WebSocket-Version:";
      size_t valueLength = lineLength - strlen(kHeader);
      const char* value = trim(line + strlen(kHeader), valueLength);
      result.versionPresent = true;
      if (hasToken(value, valueLength, "13", false)) {
        result.version13 = true;
      }
    } else if (startsWithIgnoreCase(line, lineLength,
                                    "Sec-WebSocket-Protocol:")) {
      constexpr char kHeader[] = "Sec-WebSocket-Protocol:";
      size_t valueLength = lineLength - strlen(kHeader);
      const char* value = trim(line + strlen(kHeader), valueLength);
      if (hasToken(value, valueLength, kSubprotocol, false)) {
        result.subprotocol = true;
      }
    } else if (startsWithIgnoreCase(line, lineLength, "Sec-WebSocket-Key:")) {
      constexpr char kHeader[] = "Sec-WebSocket-Key:";
      size_t valueLength = lineLength - strlen(kHeader);
      const char* value = trim(line + strlen(kHeader), valueLength);
      if (isValidWebSocketKey(value, valueLength) &&
          valueLength < sizeof(result.key)) {
        memcpy(result.key, value, valueLength);
        result.key[valueLength] = '\0';
      } else {
        result.key[0] = '\0';
      }
    } else if (startsWithIgnoreCase(line, lineLength, "Authorization:")) {
      constexpr char kHeader[] = "Authorization:";
      size_t valueLength = lineLength - strlen(kHeader);
      const char* value = trim(line + strlen(kHeader), valueLength);
      if (valueLength && valueLength < sizeof(result.authorization)) {
        memcpy(result.authorization, value, valueLength);
        result.authorization[valueLength] = '\0';
      }
    }

    if (lineEnd == end) break;
    line = lineEnd + 1;
  }

  result.valid = result.upgrade && result.connection && result.version13 &&
                 result.subprotocol && result.key[0] != '\0';
  if (result.valid) {
    result.status = HandshakeStatus::Ok;
  } else if (result.versionPresent && !result.version13) {
    result.status = HandshakeStatus::UpgradeRequired;
  } else {
    result.status = HandshakeStatus::BadRequest;
  }
  return result.valid;
}

bool websocketAccept(const char* key, char* output, size_t capacity) {
  if (!key || !output || capacity < 29) return false;
  const size_t keyLength = strlen(key);
  const size_t total = keyLength + sizeof(kGuid) - 1;
  uint8_t message[192]{};
  if (total > sizeof(message)) return false;
  memcpy(message, key, keyLength);
  memcpy(message + keyLength, kGuid, sizeof(kGuid) - 1);

  uint8_t digest[20]{};
  sha1(message, total, digest);

  char accept[29]{};
  base64(digest, sizeof(digest), accept);
  snprintf(output, capacity, "%s", accept);
  return true;
}

}  // namespace realtime
}  // namespace sentinel
