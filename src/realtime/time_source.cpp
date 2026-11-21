#include "realtime/time_source.h"

#include <stdio.h>

namespace sentinel {
namespace realtime {
namespace {

constexpr int64_t kMsPerDay = 86400000;

bool leap(int year) {
  return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

int daysInMonth(int year, int month) {
  static const int days[] = {31, 28, 31, 30, 31, 30,
                             31, 31, 30, 31, 30, 31};
  if (month == 2 && leap(year)) return 29;
  return days[month - 1];
}

bool digit(const char* text, size_t index) {
  return text[index] >= '0' && text[index] <= '9';
}

int twoDigits(const char* text, size_t index) {
  return (text[index] - '0') * 10 + (text[index + 1] - '0');
}

int fourDigits(const char* text, size_t index) {
  return (text[index] - '0') * 1000 + (text[index + 1] - '0') * 100 +
         (text[index + 2] - '0') * 10 + (text[index + 3] - '0');
}

// Days from 1970-01-01, valid for the Gregorian calendar years used by
// Protocol v1 timestamps.
int64_t daysFromCivil(int year, int month, int day) {
  year -= month <= 2 ? 1 : 0;
  const int era = (year >= 0 ? year : year - 399) / 400;
  const int yoe = year - era * 400;
  const int doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
  const int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return static_cast<int64_t>(era * 146097 + doe - 719468);
}

void civilFromDays(int64_t days, int& year, int& month, int& day) {
  days += 719468;
  const int64_t era = (days >= 0 ? days : days - 146096) / 146097;
  const int doe = static_cast<int>(days - era * 146097);
  const int yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  int y = yoe + static_cast<int>(era) * 400;
  const int doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const int mp = (5 * doy + 2) / 153;
  day = doy - (153 * mp + 2) / 5 + 1;
  month = mp + (mp < 10 ? 3 : -9);
  year = y + (month <= 2 ? 1 : 0);
}

int64_t floorDiv(int64_t value, int64_t divisor) {
  int64_t quotient = value / divisor;
  if (value % divisor < 0) --quotient;
  return quotient;
}

}  // namespace

void UtcClock::sync(int64_t epochMs, uint32_t monotonicMs) {
  epochAtSync_ = epochMs;
  monotonicAtSync_ = monotonicMs;
  synced_ = true;
}

void UtcClock::clear() {
  synced_ = false;
  epochAtSync_ = 0;
  monotonicAtSync_ = 0;
}

bool UtcClock::format(uint32_t monotonicMs, char* output,
                      size_t capacity) const {
  if (!synced_ || !output || capacity < 25) return false;
  const int64_t epoch = epochAtSync_ +
                        static_cast<int64_t>(monotonicMs) -
                        static_cast<int64_t>(monotonicAtSync_);
  const int64_t days = floorDiv(epoch, kMsPerDay);
  int64_t millis = epoch - days * kMsPerDay;
  if (millis < 0) millis += kMsPerDay;

  int year = 0, month = 0, day = 0;
  civilFromDays(days, year, month, day);
  const int hours = static_cast<int>(millis / 3600000);
  millis %= 3600000;
  const int minutes = static_cast<int>(millis / 60000);
  millis %= 60000;
  const int seconds = static_cast<int>(millis / 1000);
  const int milliseconds = static_cast<int>(millis % 1000);

  const int written = snprintf(output, capacity,
                               "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ", year,
                               month, day, hours, minutes, seconds,
                               milliseconds);
  return written > 0 && static_cast<size_t>(written) < capacity;
}

bool UtcClock::parse(const char* text, size_t length, int64_t& epochMs) {
  if (!text) return false;
  if (length != 20 && length != 22 && length != 23 && length != 24) return false;
  for (size_t i = 0; i < 19; ++i) {
    const bool separator = i == 4 || i == 7 || i == 10 || i == 13 || i == 16;
    if (separator) continue;
    if (!digit(text, i)) return false;
  }
  if (text[4] != '-' || text[7] != '-' || text[10] != 'T' ||
      text[13] != ':' || text[16] != ':') {
    return false;
  }

  size_t fractionDigits = 0;
  if (length == 20) {
    if (text[19] != 'Z') return false;
  } else {
    if (text[19] != '.' || text[length - 1] != 'Z') return false;
    fractionDigits = length - 21;
    if (fractionDigits < 1 || fractionDigits > 3) return false;
    for (size_t i = 20; i < 20 + fractionDigits; ++i) {
      if (!digit(text, i)) return false;
    }
  }

  const int year = fourDigits(text, 0);
  const int month = twoDigits(text, 5);
  const int day = twoDigits(text, 8);
  const int hour = twoDigits(text, 11);
  const int minute = twoDigits(text, 14);
  const int second = twoDigits(text, 17);
  if (year < 1970 || year > 9999 || month < 1 || month > 12 || day < 1 ||
      day > daysInMonth(year, month) || hour > 23 || minute > 59 ||
      second > 59) {
    return false;
  }

  int milliseconds = 0;
  if (fractionDigits == 1) {
    milliseconds = (text[20] - '0') * 100;
  } else if (fractionDigits == 2) {
    milliseconds = (text[20] - '0') * 100 + (text[21] - '0') * 10;
  } else if (fractionDigits == 3) {
    milliseconds = (text[20] - '0') * 100 + (text[21] - '0') * 10 +
                   (text[22] - '0');
  }

  epochMs = daysFromCivil(year, month, day) * kMsPerDay +
            static_cast<int64_t>(hour) * 3600000 +
            static_cast<int64_t>(minute) * 60000 +
            static_cast<int64_t>(second) * 1000 + milliseconds;
  return true;
}

}  // namespace realtime
}  // namespace sentinel
