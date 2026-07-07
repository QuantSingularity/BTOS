#pragma once

#include <compare>
#include <cstdint>
#include <functional>

using namespace std;

namespace btos {

struct InstrumentId {
    std::uint32_t value{0};
    constexpr auto operator<=>(const InstrumentId&) const = default;
};

struct OrderId {
    std::uint64_t value{0};
    constexpr auto operator<=>(const OrderId&) const = default;
};

struct FillId {
    std::uint64_t value{0};
    constexpr auto operator<=>(const FillId&) const = default;
};

enum class Side : std::uint8_t { Buy = 0, Sell = 1 };

constexpr int sign(Side s) { return s == Side::Buy ? 1 : -1; }

}

template <> struct std::hash<btos::InstrumentId> {
    size_t operator()(btos::InstrumentId id) const noexcept { return std::hash<std::uint32_t>{}(id.value); }
};
template <> struct std::hash<btos::OrderId> {
    size_t operator()(btos::OrderId id) const noexcept { return std::hash<std::uint64_t>{}(id.value); }
};
