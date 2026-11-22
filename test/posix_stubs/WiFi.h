#pragma once

// POSIX TCP shim with the WiFiClient/WiFiServer surface used by
// RealtimeServer. Host emulation only: it lets the real firmware WebSocket
// code run on Linux for gateway/web/iOS contract tests. No radio involved.
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#include <memory>
#include <stddef.h>
#include <stdint.h>

class WiFiClient {
 public:
  WiFiClient() = default;
  explicit WiFiClient(int fd) : handle_(std::make_shared<Handle>(fd)) {}

  operator bool() const { return handle_ && handle_->fd >= 0; }
  bool connected() const { return handle_ && handle_->fd >= 0; }

  int available() const {
    if (!connected()) return 0;
    int pending = 0;
    if (ioctl(handle_->fd, FIONREAD, &pending) < 0) return 0;
    return pending;
  }

  int read(uint8_t* output, size_t length) {
    if (!connected()) return -1;
    const ssize_t got = recv(handle_->fd, output, length, MSG_DONTWAIT);
    return got > 0 ? static_cast<int>(got) : -1;
  }

  size_t write(const uint8_t* data, size_t length) {
    if (!connected() || !data) return 0;
    size_t sent = 0;
    for (int spins = 0; sent < length && spins < 1000; ++spins) {
      const ssize_t n = send(handle_->fd, data + sent, length - sent,
                             MSG_NOSIGNAL | MSG_DONTWAIT);
      if (n > 0) {
        sent += static_cast<size_t>(n);
      } else if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
        break;
      } else {
        usleep(1000);
      }
    }
    return sent;
  }
  size_t write(const char* data, size_t length) {
    return write(reinterpret_cast<const uint8_t*>(data), length);
  }

  bool remoteClosed() const {
    if (!connected()) return true;
    uint8_t byte = 0;
    const ssize_t got = recv(handle_->fd, &byte, 1, MSG_PEEK | MSG_DONTWAIT);
    if (got == 0) return true;
    return got < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR;
  }

  void stop() {
    if (handle_ && handle_->fd >= 0) {
      close(handle_->fd);
      handle_->fd = -1;
    }
  }

 private:
  struct Handle {
    explicit Handle(int value) : fd(value) {}
    int fd;
  };
  std::shared_ptr<Handle> handle_;
};

class WiFiServer {
 public:
  explicit WiFiServer(uint16_t port = 80) : port_(port) {}
  ~WiFiServer() {
    if (fd_ >= 0) close(fd_);
  }

  void begin(uint16_t port) {
    port_ = port;
    fd_ = socket(AF_INET, SOCK_STREAM, 0);
    const int yes = 1;
    setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port_);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(fd_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0 ||
        listen(fd_, 8) < 0) {
      close(fd_);
      fd_ = -1;
      return;
    }
    fcntl(fd_, F_SETFL, fcntl(fd_, F_GETFL) | O_NONBLOCK);
  }
  bool listening() const { return fd_ >= 0; }

  WiFiClient accept() {
    if (fd_ < 0) return WiFiClient();
    const int client = ::accept(fd_, nullptr, nullptr);
    if (client < 0) return WiFiClient();
    const int yes = 1;
    setsockopt(client, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(yes));
    return WiFiClient(client);
  }

 private:
  uint16_t port_;
  int fd_ = -1;
};
