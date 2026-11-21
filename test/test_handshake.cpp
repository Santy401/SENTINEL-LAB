#include "realtime/handshake.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>

using namespace sentinel::realtime;

namespace {

struct RequestOptions {
  std::string requestLine = "GET /realtime HTTP/1.1";
  std::string upgrade = "websocket";
  std::string connection = "keep-alive, Upgrade";
  std::string key = "dGhlIHNhbXBsZSBub25jZQ==";
  std::string version = "13";
  std::string protocol = "sentinel.v1";
  bool hasVersion = true;
  bool hasProtocol = true;
  bool hasKey = true;
  bool hasAuthorization = false;
};

// Matches what browsers and RFC 6455 clients send, including headers that the
// endpoint does not need to interpret.
std::string buildRequest(const RequestOptions& options) {
  std::string request = options.requestLine + "\r\n";
  request += "Host: sentinel.local\r\n";
  request += "Origin: http://sentinel.local\r\n";
  request += "Sec-WebSocket-Extensions: permessage-deflate\r\n";
  request += "Upgrade: " + options.upgrade + "\r\n";
  request += "Connection: " + options.connection + "\r\n";
  if (options.hasKey) {
    request += "Sec-WebSocket-Key: " + options.key + "\r\n";
  }
  if (options.hasVersion) {
    request += "Sec-WebSocket-Version: " + options.version + "\r\n";
  }
  if (options.hasProtocol) {
    request += "Sec-WebSocket-Protocol: " + options.protocol + "\r\n";
  }
  if (options.hasAuthorization) {
    request += "Authorization: Bearer local-token\r\n";
  }
  request += "\r\n";
  return request;
}

bool parse(const std::string& request, HandshakeRequest& result) {
  return parseHandshake(request.data(), request.size(), result);
}

void expectValid(const RequestOptions& options) {
  const std::string request = buildRequest(options);
  HandshakeRequest parsed;
  assert(parse(request, parsed));
  assert(parsed.valid);
  assert(parsed.status == HandshakeStatus::Ok);
  assert(parsed.versionPresent);
  assert(parsed.version13);
  assert(parsed.subprotocol);
  assert(std::strcmp(parsed.key, options.key.c_str()) == 0);
}

void expectInvalid(const RequestOptions& options, HandshakeStatus status) {
  const std::string request = buildRequest(options);
  HandshakeRequest parsed;
  assert(!parse(request, parsed));
  assert(!parsed.valid);
  assert(parsed.status == status);
}

}  // namespace

int main() {
  RequestOptions browser;
  browser.hasAuthorization = true;
  expectValid(browser);
  HandshakeRequest parsed;
  assert(parse(buildRequest(browser), parsed));
  assert(std::strcmp(parsed.authorization, "Bearer local-token") == 0);

  char accept[32]{};
  assert(websocketAccept(parsed.key, accept, sizeof(accept)));
  assert(std::strcmp(accept, "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=") == 0);

  // Method and HTTP version are case-insensitive; the path is exact.
  RequestOptions lowerCase;
  lowerCase.requestLine = "get /realtime http/1.1";
  expectValid(lowerCase);
  RequestOptions wrongCasePath;
  wrongCasePath.requestLine = "GET /Realtime HTTP/1.1";
  expectInvalid(wrongCasePath, HandshakeStatus::BadRequest);
  RequestOptions wrongPath;
  wrongPath.requestLine = "GET /admin HTTP/1.1";
  expectInvalid(wrongPath, HandshakeStatus::BadRequest);
  RequestOptions longerVersion;
  longerVersion.requestLine = "GET /realtime HTTP/1.10";
  expectInvalid(longerVersion, HandshakeStatus::BadRequest);

  // Upgrade and Connection are token lists, not string prefixes.
  RequestOptions tokenLists;
  tokenLists.upgrade = "h2c, websocket";
  tokenLists.connection = "keep-alive, Upgrade";
  expectValid(tokenLists);
  RequestOptions evilUpgrade;
  evilUpgrade.upgrade = "websocket-evil";
  expectInvalid(evilUpgrade, HandshakeStatus::BadRequest);
  RequestOptions fakeConnection;
  fakeConnection.connection = "upgradefoo";
  expectInvalid(fakeConnection, HandshakeStatus::BadRequest);

  // A list containing version 13 is acceptable; other versions trigger 426.
  RequestOptions versionList;
  versionList.version = "8, 13";
  expectValid(versionList);
  RequestOptions oldVersion;
  oldVersion.version = "12";
  expectInvalid(oldVersion, HandshakeStatus::UpgradeRequired);
  RequestOptions oldVersionList;
  oldVersionList.version = "8, 12";
  expectInvalid(oldVersionList, HandshakeStatus::UpgradeRequired);
  RequestOptions missingVersion;
  missingVersion.hasVersion = false;
  expectInvalid(missingVersion, HandshakeStatus::BadRequest);

  // Only an exact sentinel.v1 token selects the subprotocol.
  RequestOptions protocolList;
  protocolList.protocol = "chat, sentinel.v1, other.v2";
  expectValid(protocolList);
  RequestOptions missingProtocol;
  missingProtocol.hasProtocol = false;
  expectInvalid(missingProtocol, HandshakeStatus::BadRequest);
  RequestOptions wrongProtocol;
  wrongProtocol.protocol = "sentinel.v2";
  expectInvalid(wrongProtocol, HandshakeStatus::BadRequest);
  RequestOptions longerProtocol;
  longerProtocol.protocol = "sentinel.v1-extra";
  expectInvalid(longerProtocol, HandshakeStatus::BadRequest);
  RequestOptions wrongCaseProtocol;
  wrongCaseProtocol.protocol = "Sentinel.V1";
  expectInvalid(wrongCaseProtocol, HandshakeStatus::BadRequest);

  // Keys must be canonical base64 for exactly 16 bytes.
  RequestOptions missingKey;
  missingKey.hasKey = false;
  expectInvalid(missingKey, HandshakeStatus::BadRequest);
  RequestOptions shortKey;
  shortKey.key = "dGhlIHNhbXBsZSBub25jZQ=";
  expectInvalid(shortKey, HandshakeStatus::BadRequest);
  RequestOptions unpaddedKey;
  unpaddedKey.key = "dGhlIHNhbXBsZSBub25jZQ";
  expectInvalid(unpaddedKey, HandshakeStatus::BadRequest);
  RequestOptions longKey;
  longKey.key = "dGhlIHNhbXBsZSBub25jZQ===";
  expectInvalid(longKey, HandshakeStatus::BadRequest);
  RequestOptions badAlphabet;
  badAlphabet.key = "dGhlIHNhbXBsZSBub25jZQ!!";
  expectInvalid(badAlphabet, HandshakeStatus::BadRequest);
  RequestOptions badPadding;
  badPadding.key = "dGhlIHNhbXBsZSBub25jZf==";
  expectInvalid(badPadding, HandshakeStatus::BadRequest);

  std::puts("test_handshake: ALL TESTS PASSED");
}
