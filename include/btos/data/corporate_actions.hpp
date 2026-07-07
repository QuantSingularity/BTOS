#pragma once

#include <algorithm>
#include <string>
#include <vector>

#include "btos/core/ids.hpp"
#include "btos/core/time.hpp"
#include "btos/data/bar.hpp"

using namespace std;

namespace btos {

struct CorporateAction {
    enum class Kind : std::uint8_t { Split, CashDividend, Delisting } kind{Kind::Split};
    InstrumentId instrument;
    TimestampNs effective;
    double ratio{1.0};
    double amount{0.0};
};

class CorporateActionBook {
  public:

    void add(CorporateAction a) {
        actions_.push_back(a);
        std::stable_sort(actions_.begin(), actions_.end(),
                         [](const CorporateAction& x, const CorporateAction& y) {
                             return x.effective < y.effective;
                         });
    }

    static CorporateActionBook from_csv(const std::string& path);

    [[nodiscard]] std::vector<CorporateAction> known_asof(InstrumentId inst, TimestampNs asof) const {
        std::vector<CorporateAction> out;
        for (const auto& a : actions_)
            if (a.instrument == inst && a.effective <= asof) out.push_back(a);
        return out;
    }

    [[nodiscard]] double adjustment_factor(InstrumentId inst, TimestampNs obs_time,
                                           TimestampNs asof) const {
        double f = 1.0;
        for (const auto& a : actions_) {
            if (a.instrument != inst || a.kind != CorporateAction::Kind::Split) continue;
            if (a.effective > obs_time && a.effective <= asof) f /= a.ratio;
        }
        return f;
    }

    [[nodiscard]] std::vector<Bar> adjust_asof(InstrumentId inst, const std::vector<Bar>& raw,
                                               TimestampNs asof) const {
        std::vector<Bar> out;
        out.reserve(raw.size());
        for (const Bar& b : raw) {
            const double f = adjustment_factor(inst, b.ts, asof);
            out.push_back(Bar{b.ts, b.open * f, b.high * f, b.low * f, b.close * f,
                              f > 0 ? b.volume / f : b.volume});
        }
        return out;
    }

    [[nodiscard]] const std::vector<CorporateAction>& all() const { return actions_; }

  private:
    std::vector<CorporateAction> actions_;
};

}
