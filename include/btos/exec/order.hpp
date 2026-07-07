#pragma once

#include <cstdint>

#include "btos/core/ids.hpp"
#include "btos/core/time.hpp"

using namespace std;

namespace btos {

enum class OrderType : std::uint8_t { Market, Limit, Stop, StopLimit };

enum class TimeInForce : std::uint8_t { Day, IOC, FOK };

enum class OrderState : std::uint8_t {
    PendingNew,
    Accepted,
    PartiallyFilled,
    Filled,
    Cancelled,
    Rejected,
};

struct Order {
    OrderId id;
    InstrumentId instrument;
    Side side{Side::Buy};
    OrderType type{OrderType::Market};
    TimeInForce tif{TimeInForce::Day};
    double quantity{0};
    double limit_price{0};
    double stop_price{0};
    TimestampNs submitted{kNoTimestamp};

    OrderState state{OrderState::PendingNew};
    double filled{0};

    [[nodiscard]] double remaining() const { return quantity - filled; }
};

}
