#pragma once

#include <array>
#include <cstdint>
#include <set>
#include <string>

#include "btos/core/time.hpp"

using namespace std;

namespace btos {

class Calendar {
  public:

    static Calendar always_open();

    static Calendar weekday_session(DurationNs open_since_midnight, DurationNs close_since_midnight);

    void add_holiday(std::int64_t utc_day) { holidays_.insert(utc_day); }

    [[nodiscard]] bool is_open(TimestampNs t) const;

    [[nodiscard]] TimestampNs next_open(TimestampNs t) const;

    [[nodiscard]] TimestampNs next_close(TimestampNs t) const;

  private:
    struct Session {
        bool open_day{false};
        std::int64_t open_ns{0};
        std::int64_t close_ns{0};
    };
    std::array<Session, 7> week_{};
    std::set<std::int64_t> holidays_;

    [[nodiscard]] const Session& session_for_day(std::int64_t day) const;
    [[nodiscard]] bool is_holiday(std::int64_t day) const { return holidays_.count(day) > 0; }
};

}
