#pragma once

#include <map>
#include <stdexcept>
#include <string>

#include "btos/core/time.hpp"

using namespace std;

namespace btos {

class FxRates {
  public:

    explicit FxRates(std::string base) : base_(std::move(base)) {}

    void set(const std::string& ccy, TimestampNs t, double to_base) {
        rates_[ccy][t.ns] = to_base;
    }

    [[nodiscard]] double to_base(const std::string& ccy, TimestampNs t) const {
        if (ccy == base_) return 1.0;
        auto it = rates_.find(ccy);
        if (it == rates_.end()) throw std::runtime_error("FxRates: unknown currency " + ccy);
        const auto& series = it->second;
        auto ub = series.upper_bound(t.ns);
        if (ub == series.begin()) throw std::runtime_error("FxRates: no rate yet for " + ccy);
        return std::prev(ub)->second;
    }

    [[nodiscard]] const std::string& base() const { return base_; }

  private:
    std::string base_;
    std::map<std::string, std::map<std::int64_t, double>> rates_;
};

}
