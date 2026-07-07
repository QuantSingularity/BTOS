#include "btos/core/time.hpp"

#include <cstdio>
#include <stdexcept>

using namespace std;

namespace btos {
namespace {

constexpr bool is_leap(int y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }

constexpr int days_in_month(int y, int m) {
    constexpr int d[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (m == 2 && is_leap(y)) return 29;
    return d[m - 1];
}

constexpr std::int64_t days_from_civil(int y, int m, int d) {
    y -= m <= 2;
    const std::int64_t era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = static_cast<unsigned>((153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1);
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<std::int64_t>(doe) - 719468;
}

constexpr void civil_from_days(std::int64_t z, int& y, int& m, int& d) {
    z += 719468;
    const std::int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = static_cast<unsigned>(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const std::int64_t yy = static_cast<std::int64_t>(yoe) + era * 400;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    d = static_cast<int>(doy - (153 * mp + 2) / 5 + 1);
    m = static_cast<int>(mp + (mp < 10 ? 3 : -9));
    y = static_cast<int>(yy + (m <= 2));
}

}

TimestampNs parse_iso8601_utc(const std::string& s) {
    int y = 0, mo = 0, d = 0, h = 0, mi = 0;
    double sec = 0.0;
    bool date_only = false;
    if (s.size() == 10) {
        if (std::sscanf(s.c_str(), "%4d-%2d-%2d", &y, &mo, &d) != 3)
            throw std::invalid_argument("bad ISO-8601 date: " + s);
        date_only = true;
    } else {
        char z = 0;
        if (std::sscanf(s.c_str(), "%4d-%2d-%2dT%2d:%2d:%lf%c", &y, &mo, &d, &h, &mi, &sec, &z) != 7 ||
            z != 'Z')
            throw std::invalid_argument("bad ISO-8601 UTC timestamp: " + s);
    }
    if (mo < 1 || mo > 12 || d < 1 || d > days_in_month(y, mo) || h > 23 || mi > 59 || sec >= 61.0)
        throw std::invalid_argument("out-of-range ISO-8601 timestamp: " + s);
    const std::int64_t day = days_from_civil(y, mo, d);
    std::int64_t ns = day * 86'400'000'000'000LL;
    if (!date_only)
        ns += (static_cast<std::int64_t>(h) * 3600 + static_cast<std::int64_t>(mi) * 60) *
                  1'000'000'000LL +
              static_cast<std::int64_t>(sec * 1e9 + 0.5);
    return TimestampNs{ns};
}

std::string format_iso8601_utc(TimestampNs t) {
    const std::int64_t day = utc_day_index(t);
    const std::int64_t rem = ns_since_midnight(t);
    int y = 0, m = 0, d = 0;
    civil_from_days(day, y, m, d);
    const auto h = static_cast<int>(rem / 3'600'000'000'000LL);
    const auto mi = static_cast<int>((rem / 60'000'000'000LL) % 60);
    const auto s = static_cast<int>((rem / 1'000'000'000LL) % 60);
    const auto frac = static_cast<long long>(rem % 1'000'000'000LL);
    char buf[48];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02d.%09lldZ", y, m, d, h, mi, s,
                  frac);
    return buf;
}

}
