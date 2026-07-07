#pragma once

#include <cstddef>
#include <vector>

#include "btos/core/time.hpp"

using namespace std;

namespace btos {

struct Split {
    std::size_t train_begin{0}, train_end{0};
    std::size_t test_begin{0}, test_end{0};
};

std::vector<Split> walk_forward_splits(std::size_t n, std::size_t train_len, std::size_t test_len);

struct PurgedFold {
    std::size_t test_begin{0}, test_end{0};
    std::vector<std::size_t> train_indices;
};
std::vector<PurgedFold> purged_kfold(std::size_t n, std::size_t k, std::size_t purge,
                                     std::size_t embargo);

double sharpe_ratio(const std::vector<double>& returns, double periods_per_year);

double probabilistic_sharpe_ratio(double observed_sharpe, double benchmark, std::size_t n_obs,
                                  double skew, double kurtosis);

double deflated_sharpe_ratio(double observed_sharpe, std::size_t n_obs, double skew,
                             double kurtosis, std::size_t n_trials, double var_trial_sharpe);

double sample_skewness(const std::vector<double>& x);

double sample_kurtosis(const std::vector<double>& x);

}
