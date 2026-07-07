#pragma once

#include <cstdint>
#include <string>

#include "btos/core/ids.hpp"
#include "btos/core/time.hpp"

using namespace std;

namespace btos {

enum class AssetClass : std::uint8_t { Equity, ETF, Future, FX, CryptoSpot };

struct Instrument {
    InstrumentId id;
    std::string symbol;
    AssetClass asset_class{AssetClass::Equity};
    std::string currency{"USD"};
    double tick_size{0.01};
    double multiplier{1.0};
    double lot_size{1.0};
    TimestampNs listing{kNoTimestamp};
    TimestampNs delisting{kMaxTimestamp};

    [[nodiscard]] bool active_at(TimestampNs t) const {
        return t >= listing && t < delisting;
    }
};

AssetClass parse_asset_class(const std::string& s);

}
