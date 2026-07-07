#pragma once

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "btos/data/instrument.hpp"

using namespace std;

namespace btos {

class InstrumentMaster {
  public:

    void add(Instrument inst);

    static InstrumentMaster from_csv(const std::string& path);

    [[nodiscard]] const Instrument& get(InstrumentId id) const;

    [[nodiscard]] std::optional<InstrumentId> find(const std::string& symbol) const;

    [[nodiscard]] std::vector<InstrumentId> active_universe(TimestampNs t) const;

    [[nodiscard]] const std::vector<Instrument>& all() const { return instruments_; }

  private:
    std::vector<Instrument> instruments_;
    std::unordered_map<std::string, InstrumentId> by_symbol_;
    std::unordered_map<std::uint32_t, std::size_t> index_by_id_;
};

}
