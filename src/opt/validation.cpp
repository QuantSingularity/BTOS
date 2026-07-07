#include "btos/opt/validation.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <stdexcept>

#include "btos/risk/risk.hpp"

using namespace std;

namespace btos {

std::vector<Split> walk_forward_splits(std::size_t n, std::size_t train_len, std::size_t test_len) {
    if (train_len == 0 || test_len == 0 || train_len + test_len > n)
        throw std::invalid_argument("walk_forward: infeasible window sizes");
    std::vector<Split> out;
    std::size_t start = 0;
    while (start + train_len + test_len <= n) {
        Split s;
        s.train_begin = start;
        s.train_end = start + train_len;
        s.test_begin = s.train_end;
        s.test_end = s.test_begin + test_len;
        out.push_back(s);
        start += test_len;
    }
    return out;
}

std::vector<PurgedFold> purged_kfold(std::size_t n, std::size_t k, std::size_t purge,
                                     std::size_t embargo) {
    if (k < 2 || k > n) throw std::invalid_argument("purged_kfold: invalid k");
    std::vector<PurgedFold> folds;
    const std::size_t base = n / k;
    std::size_t begin = 0;
    for (std::size_t f = 0; f < k; ++f) {
        const std::size_t len = base + (f < n % k ? 1 : 0);
        PurgedFold fold;
        fold.test_begin = begin;
        fold.test_end = begin + len;
        const std::size_t lo = fold.test_begin > purge ? fold.test_begin - purge : 0;
        const std::size_t hi = std::min(n, fold.test_end + purge + embargo);
        for (std::size_t i = 0; i < n; ++i)
            if (i < lo || i >= hi) fold.train_indices.push_back(i);
        folds.push_back(std::move(fold));
        begin += len;
    }
    return folds;
}

double sharpe_ratio(const std::vector<double>& returns, double periods_per_year) {
    if (returns.size() < 2) return 0;
    const double mean = std::accumulate(returns.begin(), returns.end(), 0.0) /
                        static_cast<double>(returns.size());
    double var = 0;
    for (double r : returns) var += (r - mean) * (r - mean);
    var /= static_cast<double>(returns.size() - 1);
    const double sd = std::sqrt(var);
    if (sd <= 1e-15) return 0;
    return mean / sd * std::sqrt(periods_per_year);
}

double sample_skewness(const std::vector<double>& x) {
    const std::size_t n = x.size();
    if (n < 3) return 0;
    const double mean = std::accumulate(x.begin(), x.end(), 0.0) / static_cast<double>(n);
    double m2 = 0, m3 = 0;
    for (double v : x) {
        const double d = v - mean;
        m2 += d * d;
        m3 += d * d * d;
    }
    m2 /= static_cast<double>(n);
    m3 /= static_cast<double>(n);
    if (m2 <= 1e-15) return 0;
    return m3 / std::pow(m2, 1.5);
}

double sample_kurtosis(const std::vector<double>& x) {
    const std::size_t n = x.size();
    if (n < 4) return 3;
    const double mean = std::accumulate(x.begin(), x.end(), 0.0) / static_cast<double>(n);
    double m2 = 0, m4 = 0;
    for (double v : x) {
        const double d = v - mean;
        m2 += d * d;
        m4 += d * d * d * d;
    }
    m2 /= static_cast<double>(n);
    m4 /= static_cast<double>(n);
    if (m2 <= 1e-15) return 3;
    return m4 / (m2 * m2);
}

double probabilistic_sharpe_ratio(double observed_sharpe, double benchmark, std::size_t n_obs,
                                  double skew, double kurtosis) {
    if (n_obs < 2) return 0;
    const double n = static_cast<double>(n_obs);
    const double num = (observed_sharpe - benchmark) * std::sqrt(n - 1.0);
    const double den = std::sqrt(std::max(
        1e-12, 1.0 - skew * observed_sharpe +
                   (kurtosis - 1.0) / 4.0 * observed_sharpe * observed_sharpe));
    return normal_cdf(num / den);
}

double deflated_sharpe_ratio(double observed_sharpe, std::size_t n_obs, double skew,
                             double kurtosis, std::size_t n_trials, double var_trial_sharpe) {
    if (n_trials < 1) throw std::invalid_argument("dsr: need at least one trial");
    const double gamma = 0.5772156649015329;
    const double k = std::max<double>(2.0, static_cast<double>(n_trials));
    const double sd = std::sqrt(std::max(1e-12, var_trial_sharpe));
    const double z1 = inverse_normal_cdf(1.0 - 1.0 / k);
    const double z2 = inverse_normal_cdf(1.0 - 1.0 / (k * std::exp(1.0)));
    const double sr0 = sd * ((1.0 - gamma) * z1 + gamma * z2);
    return probabilistic_sharpe_ratio(observed_sharpe, sr0, n_obs, skew, kurtosis);
}

}
