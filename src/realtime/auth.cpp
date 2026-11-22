#include "realtime/auth.h"

#include <ctype.h>
#include <string.h>

namespace sentinel {
namespace realtime {
namespace {

bool equalConstantTime(const char* left, const char* right, size_t length) {
  unsigned char difference = 0;
  for (size_t i = 0; i < length; ++i) {
    difference |= static_cast<unsigned char>(left[i]) ^
                  static_cast<unsigned char>(right[i]);
  }
  return difference == 0;
}

}  // namespace

bool AuthGuard::verifyBearer(const char* header, size_t length) const {
  if (!token_ || !*token_ || !header) return false;
  static const char prefix[] = "bearer ";
  constexpr size_t prefixLength = sizeof(prefix) - 1;
  if (length <= prefixLength) return false;
  for (size_t i = 0; i < prefixLength; ++i) {
    const unsigned char actual = static_cast<unsigned char>(header[i]);
    if (tolower(actual) != static_cast<unsigned char>(prefix[i])) return false;
  }

  size_t tokenLength = length - prefixLength;
  while (tokenLength && (header[prefixLength + tokenLength - 1] == '\r' ||
                         header[prefixLength + tokenLength - 1] == '\n' ||
                         header[prefixLength + tokenLength - 1] == ' ')) {
    --tokenLength;
  }
  const size_t configuredLength = strlen(token_);
  if (tokenLength != configuredLength) return false;
  return equalConstantTime(header + prefixLength, token_, tokenLength);
}

bool AuthGuard::verifyToken(const char* token, size_t length) const {
  if (!token_ || !*token_ || !token || length == 0) return false;
  if (length != strlen(token_)) return false;
  return equalConstantTime(token, token_, length);
}

bool AuthGuard::privilegedRole(const char* role) {
  return role && strcmp(role, "gateway") == 0;
}

}  // namespace realtime
}  // namespace sentinel
