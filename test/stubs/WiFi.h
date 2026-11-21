#pragma once

// Minimal in-memory WiFi stubs used only by host tests for the WebSocket
// server lifecycle. No radio, network or Arduino runtime is involved.
#include <stddef.h>
#include <stdint.h>

#include <memory>
#include <string>
#include <vector>

inline std::vector<class WiFiClient> gPendingWiFiClients;

class WiFiClient {
 public:
  struct State {
    std::vector<uint8_t> rx;
    std::vector<uint8_t> tx;
    bool active = true;
  };

  WiFiClient() = default;
  explicit WiFiClient(std::shared_ptr<State> state) : state_(state) {}

  static WiFiClient create() { return WiFiClient(std::make_shared<State>()); }

  void push(const void* data, size_t length) {
    if (!state_) return;
    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    state_->rx.insert(state_->rx.end(), bytes, bytes + length);
  }

  void push(const std::string& text) { push(text.data(), text.size()); }

  void push(const std::vector<uint8_t>& bytes) {
    push(bytes.data(), bytes.size());
  }

  operator bool() const { return state_ && state_->active; }
  bool connected() const { return state_ && state_->active; }

  int available() const {
    return state_ && state_->active ? static_cast<int>(state_->rx.size()) : 0;
  }

  int read(uint8_t* output, size_t length) {
    if (!connected() || !output || length == 0 || state_->rx.empty()) return -1;
    const size_t count =
        length < state_->rx.size() ? length : state_->rx.size();
    for (size_t i = 0; i < count; ++i) output[i] = state_->rx[i];
    state_->rx.erase(state_->rx.begin(), state_->rx.begin() + count);
    return static_cast<int>(count);
  }

  size_t write(const uint8_t* data, size_t length) {
    if (!connected() || !data) return 0;
    state_->tx.insert(state_->tx.end(), data, data + length);
    return length;
  }

  size_t write(const char* data, size_t length) {
    return write(reinterpret_cast<const uint8_t*>(data), length);
  }

  size_t write(const char* data) {
    size_t length = 0;
    while (data[length]) ++length;
    return write(data, length);
  }

  void stop() {
    if (state_) state_->active = false;
  }

  std::string sent() const {
    if (!state_) return "";
    return std::string(state_->tx.begin(), state_->tx.end());
  }

  void clearSent() {
    if (state_) state_->tx.clear();
  }

 private:
  std::shared_ptr<State> state_;
};

class WiFiServer {
 public:
  explicit WiFiServer(uint16_t port = 80) : port_(port) {}

  void begin(uint16_t port) { port_ = port; }

  WiFiClient accept() {
    (void)port_;
    if (gPendingWiFiClients.empty()) return WiFiClient();
    WiFiClient client = gPendingWiFiClients.front();
    gPendingWiFiClients.erase(gPendingWiFiClients.begin());
    return client;
  }

 private:
  uint16_t port_;
};
