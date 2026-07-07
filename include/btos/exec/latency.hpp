#pragma once

#include <concepts>
#include <random>

#include "btos/core/time.hpp"

using namespace std;

namespace btos {

class ILatencyModel {
  public:
    virtual ~ILatencyModel() = default;

    virtual DurationNs next() = 0;
};

template <typename T>
concept LatencyPolicy = requires(T t) {
    { t.next() } -> std::same_as<DurationNs>;
};

class FixedLatency final : public ILatencyModel {
  public:
    explicit FixedLatency(DurationNs d) : d_(d) {}
    DurationNs next() override { return d_; }

  private:
    DurationNs d_;
};

class UniformLatency final : public ILatencyModel {
  public:
    UniformLatency(DurationNs lo, DurationNs hi, std::mt19937_64 rng)
        : dist_(lo.ns, hi.ns), rng_(rng) {}
    DurationNs next() override { return DurationNs{dist_(rng_)}; }

  private:
    std::uniform_int_distribution<std::int64_t> dist_;
    std::mt19937_64 rng_;
};

class LognormalLatency final : public ILatencyModel {
  public:
    LognormalLatency(double log_mean, double log_sigma, DurationNs floor, std::mt19937_64 rng)
        : dist_(log_mean, log_sigma), floor_(floor), rng_(rng) {}
    DurationNs next() override {
        auto v = static_cast<std::int64_t>(dist_(rng_));
        return DurationNs{v < floor_.ns ? floor_.ns : v};
    }

  private:
    std::lognormal_distribution<double> dist_;
    DurationNs floor_;
    std::mt19937_64 rng_;
};

static_assert(LatencyPolicy<FixedLatency>);

}
