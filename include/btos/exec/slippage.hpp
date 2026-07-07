#pragma once

#include <cmath>

#include "btos/core/ids.hpp"

using namespace std;

namespace btos {

struct SlippageContext {
    double reference_price{0};
    double quantity{0};
    Side side{Side::Buy};
    double adv{0};
    double sigma{0};
};

class ISlippageModel {
  public:
    virtual ~ISlippageModel() = default;

    [[nodiscard]] virtual double apply(const SlippageContext& ctx) const = 0;
};

class FixedBpsSlippage final : public ISlippageModel {
  public:
    explicit FixedBpsSlippage(double bps) : bps_(bps) {}
    [[nodiscard]] double apply(const SlippageContext& ctx) const override {
        const double adj = ctx.reference_price * bps_ * 1e-4;
        return ctx.reference_price + sign(ctx.side) * adj;
    }

  private:
    double bps_;
};

class SquareRootImpact final : public ISlippageModel {
  public:
    explicit SquareRootImpact(double k) : k_(k) {}
    [[nodiscard]] double apply(const SlippageContext& ctx) const override {
        if (ctx.adv <= 0) return ctx.reference_price;
        const double frac = k_ * ctx.sigma * std::sqrt(ctx.quantity / ctx.adv);
        return ctx.reference_price * (1.0 + sign(ctx.side) * frac);
    }

  private:
    double k_;
};

}
