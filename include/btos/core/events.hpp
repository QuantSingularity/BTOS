#pragma once

#include <cstdint>
#include <string>
#include <variant>

#include "btos/core/ids.hpp"
#include "btos/core/time.hpp"

using namespace std;

namespace btos {

enum class EventPriority : std::uint8_t {
    Session = 0,
    CorporateAction = 1,
    MarketData = 2,
    OrderAck = 3,
    Fill = 4,
    Timer = 5,
};

struct BarEvent {
    InstrumentId instrument;
    double open{0}, high{0}, low{0}, close{0};
    double volume{0};
    DurationNs period{0};
};

struct TickEvent {
    InstrumentId instrument;
    double price{0};
    double size{0};
    Side aggressor{Side::Buy};
};

struct QuoteEvent {
    InstrumentId instrument;
    double bid{0}, ask{0};
    double bid_size{0}, ask_size{0};
};

struct OrderArrivalEvent {
    OrderId order;
};

struct OrderAckEvent {
    OrderId order;
    bool accepted{true};
    std::string reason;
};

struct CancelAckEvent {
    OrderId order;
    bool cancelled{true};
};

struct FillEvent {
    FillId fill;
    OrderId order;
    InstrumentId instrument;
    Side side{Side::Buy};
    double quantity{0};
    double price{0};
    double commission{0};
};

struct TimerEvent {
    std::uint64_t token{0};
};

struct SessionEvent {
    enum class Kind : std::uint8_t { Open, Close, Halt, Resume, CircuitBreaker } kind{Kind::Open};
    InstrumentId instrument;
};

struct CorporateActionEvent {
    enum class Kind : std::uint8_t { Split, CashDividend, Delisting } kind{Kind::Split};
    InstrumentId instrument;
    double ratio{1.0};
    double amount{0.0};
};

using EventPayload = std::variant<BarEvent, TickEvent, QuoteEvent, OrderArrivalEvent,
                                  OrderAckEvent, CancelAckEvent, FillEvent, TimerEvent,
                                  SessionEvent, CorporateActionEvent>;

struct Event {
    TimestampNs ts;
    EventPriority priority{EventPriority::MarketData};
    std::uint64_t seq{0};
    EventPayload payload;
};

struct EventOrder {
    bool operator()(const Event& a, const Event& b) const {
        if (a.ts != b.ts) return a.ts < b.ts;
        if (a.priority != b.priority) return a.priority < b.priority;
        return a.seq < b.seq;
    }
};

constexpr EventPriority default_priority(const EventPayload& p) {
    switch (p.index()) {
        case 0: case 1: case 2: return EventPriority::MarketData;
        case 3: return EventPriority::MarketData;
        case 4: case 5: return EventPriority::OrderAck;
        case 6: return EventPriority::Fill;
        case 7: return EventPriority::Timer;
        case 8: return EventPriority::Session;
        default: return EventPriority::CorporateAction;
    }
}

}
