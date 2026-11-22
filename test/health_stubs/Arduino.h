#pragma once

#include <stddef.h>
#include <stdint.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <string>

#define PROGMEM

extern uint32_t gTestNowMs;
extern uint32_t gTestFreeHeap;

inline uint32_t millis() { return gTestNowMs; }
inline void configTime(long, long, const char*) {}
inline uint32_t esp_random() { return 0x1234ABCDu; }

// Small enough for host checks, but exposes the operations used by web_rf.cpp
// and the ArduinoJson sinks used there.
class String {
 public:
  std::string value;
  mutable size_t readPosition = 0;

  String() = default;
  String(const char* text) : value(text ? text : "") {}
  String(char* text) : value(text ? text : "") {}
  String(const std::string& text) : value(text) {}
  template <typename T>
  explicit String(T number) : value(std::to_string(number)) {}
  String(double number, int decimals) {
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.*f", decimals, number);
    value = buffer;
  }

  int read() const {
    return readPosition < value.size()
               ? static_cast<unsigned char>(value[readPosition++])
               : -1;
  }
  int available() const {
    return static_cast<int>(value.size() - readPosition);
  }
  size_t write(uint8_t c) {
    value.push_back(static_cast<char>(c));
    return 1;
  }
  size_t write(const uint8_t* data, size_t length) {
    value.append(reinterpret_cast<const char*>(data), length);
    return length;
  }

  bool concat(const char* text) {
    value += text;
    return true;
  }
  const char* c_str() const { return value.c_str(); }
  const char* data() const { return value.c_str(); }
  size_t length() const { return value.size(); }
  size_t size() const { return value.size(); }
  char operator[](size_t index) const { return value[index]; }
  String substring(size_t start) const { return String(value.substr(start)); }

  void replace(const char* from, const char* to) {
    const std::string f(from);
    const std::string t(to);
    size_t pos = 0;
    while ((pos = value.find(f, pos)) != std::string::npos) {
      value.replace(pos, f.size(), t);
      pos += t.size();
    }
  }
  void trim() {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
      value.clear();
      return;
    }
    const auto last = value.find_last_not_of(" \t\r\n");
    value = value.substr(first, last - first + 1);
  }

  template <typename T>
  String& operator+=(const T& other) {
    value += String(other).value;
    return *this;
  }
  String& operator+=(const String& other) {
    value += other.value;
    return *this;
  }
  String& operator+=(const char* other) {
    value += other;
    return *this;
  }
};

template <typename T>
String operator+(const String& left, const T& right) {
  String result = left;
  result += right;
  return result;
}

template <typename T>
String operator+(const T& left, const String& right) {
  String result(left);
  result += right;
  return result;
}

inline String operator+(const String& left, const String& right) {
  String result = left;
  result += right;
  return result;
}
