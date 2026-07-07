#pragma once

#include <algorithm>
#include <cmath>

using namespace std;

namespace btos {

class ICommissionModel {
  public:
    virtual ~ICommissionModel() = default;

    [[nodiscard]] virtual double commission(double quantity, double price,
                                            double multiplier) const = 0;
};

class NoCommission final : public ICommissionModel {
  public:
    [[nodiscard]] double commission(double, double, double) const override { return 0.0; }
};

class PerShareCommission final : public ICommissionModel {
  public:
    explicit PerShareCommission(double per_share, double minimum = 0.0)
        : per_share_(per_share), minimum_(minimum) {}
    [[nodiscard]] double commission(double quantity, double, double) const override {
        return std::max(minimum_, std::abs(quantity) * per_share_);
    }

  private:
    double per_share_, minimum_;
};

class BpsCommission final : public ICommissionModel {
  public:
    explicit BpsCommission(double bps) : bps_(bps) {}
    [[nodiscard]] double commission(double quantity, double price, double multiplier) const override {
        return std::abs(quantity) * price * multiplier * bps_ * 1e-4;
    }

  private:
    double bps_;
};

}
