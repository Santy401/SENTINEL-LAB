#pragma once

#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "Arduino.h"

constexpr int HTTP_GET = 0;
constexpr int HTTP_POST = 1;
constexpr int CONTENT_LENGTH_UNKNOWN = 0;

class WebServer {
 public:
  static constexpr int HTTP_GET = 0;
  static constexpr int HTTP_POST = 1;

  using Handler = std::function<void()>;

  void on(const char* path, int method, Handler handler) {
    routes.push_back({path, method, std::move(handler)});
  }

  const String& arg(const char*) { return plainArg; }
  void sendHeader(const char*, const char*) {}
  void send(int code, const char* type, const String& body) {
    lastCode = code;
    lastType = type;
    lastBody = body.value;
  }
  void send_P(int, const char*, const char*) {}
  void setContentLength(int) {}
  void sendContent(const String&) {}
  void sendContent(const char*) {}

  struct Route {
    std::string path;
    int method = 0;
    Handler handler;
  };
  std::vector<Route> routes;
  String plainArg;
  int lastCode = 0;
  std::string lastType;
  std::string lastBody;
};
