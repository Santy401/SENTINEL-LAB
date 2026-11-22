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
  void sendHeader(const char* name, const char* value) {
    headers.push_back(std::string(name) + ": " + value);
  }
  void send(int code, const char* type, const String& body) {
    lastCode = code;
    lastType = type;
    lastBody = body.value;
  }
  void send_P(int code, const char* type, const char* body) {
    lastCode = code;
    lastType = type;
    lastBody = body;
  }
  void send_P(int code, const char* type, const char* body, size_t length) {
    lastCode = code;
    lastType = type;
    lastBody.assign(body, length);
  }
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
  std::vector<std::string> headers;

  Route* find(const std::string& path) {
    for (auto& route : routes) {
      if (route.path == path) return &route;
    }
    return nullptr;
  }
};
