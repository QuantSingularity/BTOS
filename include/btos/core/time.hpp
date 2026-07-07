#pragma once

#include <chrono>
#include <compare>
#include <cstdint>
#include <string>

using namespace std;

namespace btos {

struct TimestampNs {
    std::int64_t ns{0};
    constexpr auto operator<=>(const TimestampNs&) const = default;
};

struct DurationNs {
    std::int64_t ns{0};
    constexpr auto operator<=>(const DurationNs&) const = default;
};

constexpr TimestampNs operator+(TimestampNs t, DurationNs d) { return {t.ns + d.ns}; }
constexpr TimestampNs operator-(TimestampNs t, DurationNs d) { return {t.ns - d.ns}; }
constexpr DurationNs operator-(TimestampNs a, TimestampNs b) { return {a.ns - b.ns}; }
constexpr DurationNs operator+(DurationNs a, DurationNs b) { return {a.ns + b.ns}; }
constexpr DurationNs operator*(DurationNs d, std::int64_t k) { return {d.ns * k}; }

constexpr DurationNs nanoseconds(std::int64_t v) { return {v}; }
constexpr DurationNs microseconds(std::int64_t v) { return {v * 1'000}; }
constexpr DurationNs milliseconds(std::int64_t v) { return {v * 1'000'000}; }
constexpr DurationNs seconds(std::int64_t v) { return {v * 1'000'000'000}; }
constexpr DurationNs minutes(std::int64_t v) { return {v * 60'000'000'000}; }
constexpr DurationNs hours(std::int64_t v) { return {v * 3'600'000'000'000}; }
constexpr DurationNs days(std::int64_t v) { return {v * 86'400'000'000'000}; }

inline constexpr TimestampNs kNoTimestamp{INT64_MIN};

inline constexpr TimestampNs kMaxTimestamp{INT64_MAX};

TimestampNs parse_iso8601_utc(const std::string& s);

std::string format_iso8601_utc(TimestampNs t);

constexpr std::int64_t utc_day_index(TimestampNs t) {
    const std::int64_t d = 86'400'000'000'000;
    std::int64_t q = t.ns / d;
    if (t.ns % d < 0) --q;
    return q;
}

constexpr std::int64_t ns_since_midnight(TimestampNs t) {
    const std::int64_t d = 86'400'000'000'000;
    std::int64_t r = t.ns % d;
    if (r < 0) r += d;
    return r;
}

}
