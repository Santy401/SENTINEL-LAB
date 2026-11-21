#pragma once

#include <stddef.h>
#include <stdint.h>

#include <memory>
#include <string>
#include <vector>

#include "Arduino.h"

class WiFiClient {
 public:
  WiFiClient() = default;
  explicit WiFiClient(std::shared_ptr<int> state) : state_(state) {}
  operator bool() const { return state_ && *state_; }
  bool connected() const { return state_ && *state_; }
  int available() const { return 0; }
  int read(uint8_t*, size_t) { return -1; }
  size_t write(const uint8_t* data, size_t length) {
    (void)data;
    return length;
  }
  void stop() {}

 private:
  std::shared_ptr<int> state_;
};

class WiFiServer {
 public:
  explicit WiFiServer(uint16_t port = 80) : port_(port) {}
  void begin(uint16_t port) { port_ = port; }
  WiFiClient accept() { return WiFiClient(); }

 private:
  uint16_t port_;
};

class IPAddress {
 public:
  String toString() const { return String("192.168.4.1"); }
};

constexpr int WIFI_AP_STA = 3;
constexpr int WL_CONNECTED = 3;

class WiFiClass {
 public:
  int getMode() const { return WIFI_AP_STA; }
  int status() const { return WL_CONNECTED; }
  String softAPSSID() const { return String("SENTINEL-LAB"); }
  IPAddress softAPIP() const { return IPAddress(); }
};

extern WiFiClass WiFi;
