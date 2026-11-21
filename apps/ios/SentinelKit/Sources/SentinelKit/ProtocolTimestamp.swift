import Foundation

/// Strict Protocol v1 date-time subset: `YYYY-MM-DDTHH:MM:SS(.f{1,3})?Z`.
///
/// Pure integer arithmetic (no Calendar/DateFormatter) keeps parsing and
/// formatting identical on Darwin and Linux and matches the firmware
/// implementation in `src/realtime/time_source.cpp`.
public enum ProtocolTimestamp {
    /// Parses a Protocol v1 timestamp and returns milliseconds since the Unix epoch.
    public static func parseMilliseconds(_ text: String) -> Int64? {
        let bytes = Array(text.utf8)
        guard [20, 22, 23, 24].contains(bytes.count) else { return nil }

        func isDigit(_ index: Int) -> Bool { bytes[index] >= 0x30 && bytes[index] <= 0x39 }
        func value(_ index: Int) -> Int { Int(bytes[index]) - 0x30 }
        func number(_ start: Int, _ length: Int) -> Int {
            (start..<(start + length)).reduce(0) { $0 * 10 + value($1) }
        }

        let separators: [Int: UInt8] = [4: 0x2D, 7: 0x2D, 10: 0x54, 13: 0x3A, 16: 0x3A]
        for index in 0..<19 {
            if let expected = separators[index] {
                guard bytes[index] == expected else { return nil }
            } else if !isDigit(index) {
                return nil
            }
        }

        var milliseconds = 0
        if bytes.count == 20 {
            guard bytes[19] == 0x5A else { return nil }
        } else {
            guard bytes[19] == 0x2E, bytes[bytes.count - 1] == 0x5A else { return nil }
            let digits = bytes.count - 21
            guard (1...3).contains(digits) else { return nil }
            for index in 20..<(20 + digits) {
                guard isDigit(index) else { return nil }
            }
            milliseconds = number(20, digits)
            for _ in digits..<3 { milliseconds *= 10 }
        }

        let year = number(0, 4)
        let month = number(5, 2)
        let day = number(8, 2)
        let hour = number(11, 2)
        let minute = number(14, 2)
        let second = number(17, 2)
        guard year >= 1970, (1...12).contains(month), day >= 1,
              day <= daysInMonth(year: year, month: month),
              hour <= 23, minute <= 59, second <= 59 else { return nil }

        let days = daysFromCivil(year: year, month: month, day: day)
        return days * 86_400_000 + Int64(hour) * 3_600_000 + Int64(minute) * 60_000
            + Int64(second) * 1_000 + Int64(milliseconds)
    }

    public static func parse(_ text: String) -> Date? {
        parseMilliseconds(text).map { Date(timeIntervalSince1970: Double($0) / 1_000) }
    }

    /// Formats with exactly three fractional digits, as the firmware does.
    public static func format(_ date: Date) -> String {
        formatMilliseconds(Int64((date.timeIntervalSince1970 * 1_000).rounded(.down)))
    }

    public static func formatMilliseconds(_ epochMs: Int64) -> String {
        let msPerDay: Int64 = 86_400_000
        var days = epochMs / msPerDay
        var remainder = epochMs % msPerDay
        if remainder < 0 {
            remainder += msPerDay
            days -= 1
        }
        let (year, month, day) = civilFromDays(days)
        let hours = remainder / 3_600_000
        let minutes = (remainder % 3_600_000) / 60_000
        let seconds = (remainder % 60_000) / 1_000
        let millis = remainder % 1_000
        return pad(year, 4) + "-" + pad(month, 2) + "-" + pad(day, 2) + "T"
            + pad(Int(hours), 2) + ":" + pad(Int(minutes), 2) + ":" + pad(Int(seconds), 2)
            + "." + pad(Int(millis), 3) + "Z"
    }

    private static func pad(_ value: Int, _ width: Int) -> String {
        let text = String(value)
        return text.count >= width ? text : String(repeating: "0", count: width - text.count) + text
    }

    private static func isLeap(_ year: Int) -> Bool {
        (year % 4 == 0 && year % 100 != 0) || year % 400 == 0
    }

    private static func daysInMonth(year: Int, month: Int) -> Int {
        let days = [31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31]
        return month == 2 && isLeap(year) ? 29 : days[month - 1]
    }

    // Howard Hinnant's civil calendar algorithms.
    private static func daysFromCivil(year: Int, month: Int, day: Int) -> Int64 {
        let y = month <= 2 ? year - 1 : year
        let era = (y >= 0 ? y : y - 399) / 400
        let yoe = y - era * 400
        let doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1
        let doe = yoe * 365 + yoe / 4 - yoe / 100 + doy
        return Int64(era * 146_097 + doe - 719_468)
    }

    private static func civilFromDays(_ value: Int64) -> (Int, Int, Int) {
        let z = value + 719_468
        let era = (z >= 0 ? z : z - 146_096) / 146_097
        let doe = Int(z - era * 146_097)
        let yoe = (doe - doe / 1_460 + doe / 36_524 - doe / 146_096) / 365
        let doy = doe - (365 * yoe + yoe / 4 - yoe / 100)
        let mp = (5 * doy + 2) / 153
        let day = doy - (153 * mp + 2) / 5 + 1
        let month = mp + (mp < 10 ? 3 : -9)
        let year = yoe + Int(era) * 400 + (month <= 2 ? 1 : 0)
        return (year, month, day)
    }
}
