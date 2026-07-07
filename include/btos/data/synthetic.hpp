#pragma once

#include <random>
#include <vector>

#include "btos/data/bar.hpp"

using namespace std;

namespace btos {

struct GbmParams {
    double s0{100.0};
    double mu{0.05};
    double sigma{0.2};
    double year_ns{252.0 * 6.5 * 3600.0 * 1e9};
    double base_volume{1e5};
};

std::vector<Bar> generate_gbm_bars(std::mt19937_64& rng, const GbmParams& p, TimestampNs start,
                                   DurationNs period, std::size_t n);

struct HestonParams {
    double s0{100.0};
    double v0{0.04};
    double mu{0.05};
    double kappa{1.5};
    double theta{0.04};
    double xi{0.5};
    double rho{-0.7};
    double year_ns{252.0 * 6.5 * 3600.0 * 1e9};
    double base_volume{1e5};
};

std::vector<Bar> generate_heston_bars(std::mt19937_64& rng, const HestonParams& p,
                                      TimestampNs start, DurationNs period, std::size_t n);

struct PoissonTradeParams {
    double lambda_per_second{5.0};
    double size_mean{100.0};
    double spread_bps{2.0};
};

std::vector<Trade> generate_poisson_trades(std::mt19937_64& rng, const PoissonTradeParams& p,
                                           const std::vector<Bar>& mid_bars, TimestampNs start,
                                           TimestampNs end);

}
