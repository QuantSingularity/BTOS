#include "btos/opt/advanced.hpp"

#include <algorithm>
#include <cmath>
#include <random>
#include <stdexcept>

#include "btos/risk/risk.hpp"

using namespace std;

namespace btos {
namespace {

std::vector<double> to_unit(const ParamMap& m, const std::vector<ParamSpec>& space) {
    std::vector<double> x(space.size());
    for (std::size_t d = 0; d < space.size(); ++d) {
        const double span = space[d].hi - space[d].lo;
        x[d] = span > 0 ? (m.at(space[d].name) - space[d].lo) / span : 0.0;
    }
    return x;
}

ParamMap from_unit(const std::vector<double>& x, const std::vector<ParamSpec>& space) {
    ParamMap m;
    for (std::size_t d = 0; d < space.size(); ++d) {
        double v = space[d].lo + (space[d].hi - space[d].lo) * std::clamp(x[d], 0.0, 1.0);
        if (space[d].step > 0)
            v = space[d].lo + std::round((v - space[d].lo) / space[d].step) * space[d].step;
        m[space[d].name] = std::clamp(v, space[d].lo, space[d].hi);
    }
    return m;
}

double rbf(const std::vector<double>& a, const std::vector<double>& b, double ls, double sig2) {
    double d2 = 0;
    for (std::size_t i = 0; i < a.size(); ++i) d2 += (a[i] - b[i]) * (a[i] - b[i]);
    return sig2 * std::exp(-0.5 * d2 / (ls * ls));
}

bool cholesky(std::vector<double>& a, std::size_t n) {
    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t j = 0; j <= i; ++j) {
            double s = a[i * n + j];
            for (std::size_t k = 0; k < j; ++k) s -= a[i * n + k] * a[j * n + k];
            if (i == j) {
                if (s <= 0) return false;
                a[i * n + i] = std::sqrt(s);
            } else {
                a[i * n + j] = s / a[j * n + j];
            }
        }
        for (std::size_t j = i + 1; j < n; ++j) a[i * n + j] = 0;
    }
    return true;
}

std::vector<double> chol_solve(const std::vector<double>& L, std::vector<double> b, std::size_t n) {
    for (std::size_t i = 0; i < n; ++i) {
        double s = b[i];
        for (std::size_t k = 0; k < i; ++k) s -= L[i * n + k] * b[k];
        b[i] = s / L[i * n + i];
    }
    for (std::size_t ii = n; ii-- > 0;) {
        double s = b[ii];
        for (std::size_t k = ii + 1; k < n; ++k) s -= L[k * n + ii] * b[k];
        b[ii] = s / L[ii * n + ii];
    }
    return b;
}

}

std::vector<Evaluation> BayesianGpOptimizer::optimize(const std::vector<ParamSpec>& space,
                                                      const Objective& objective,
                                                      std::size_t budget, std::uint64_t seed,
                                                      IEvaluationBackend& backend) {
    if (space.empty()) throw std::invalid_argument("bayes: empty parameter space");
    std::mt19937_64 rng(seed);
    std::uniform_real_distribution<double> u(0.0, 1.0);
    const std::size_t dim = space.size();
    const std::size_t n_init = std::min(budget, std::max<std::size_t>(2, init_random_));

    std::vector<ParamMap> init;
    for (std::size_t i = 0; i < n_init; ++i) {
        std::vector<double> x(dim);
        for (auto& xi : x) xi = u(rng);
        init.push_back(from_unit(x, space));
    }
    std::vector<Evaluation> evals = backend.evaluate(init, objective);

    const double ls = 0.2;
    const double jitter = 1e-8;
    while (evals.size() < budget) {
        const std::size_t n = evals.size();
        std::vector<std::vector<double>> X(n);
        std::vector<double> y(n);
        double best = -1e300;
        for (std::size_t i = 0; i < n; ++i) {
            X[i] = to_unit(evals[i].params, space);
            y[i] = evals[i].objective;
            best = std::max(best, y[i]);
        }
        double ymean = 0;
        for (double v : y) ymean += v;
        ymean /= static_cast<double>(n);
        double yvar = 0;
        for (double v : y) yvar += (v - ymean) * (v - ymean);
        yvar = n > 1 ? yvar / static_cast<double>(n - 1) : 1.0;
        const double sig2 = std::max(yvar, 1e-12);

        std::vector<double> K(n * n);
        for (std::size_t i = 0; i < n; ++i)
            for (std::size_t j = 0; j < n; ++j)
                K[i * n + j] = rbf(X[i], X[j], ls, sig2) + (i == j ? jitter * sig2 : 0.0);
        if (!cholesky(K, n)) break;
        std::vector<double> yc(n);
        for (std::size_t i = 0; i < n; ++i) yc[i] = y[i] - ymean;
        const std::vector<double> alpha = chol_solve(K, yc, n);

        double best_ei = -1;
        std::vector<double> best_x(dim, 0.5);
        for (int c = 0; c < 256; ++c) {
            std::vector<double> x(dim);
            for (auto& xi : x) xi = u(rng);
            std::vector<double> k(n);
            for (std::size_t i = 0; i < n; ++i) k[i] = rbf(x, X[i], ls, sig2);
            double mu = ymean;
            for (std::size_t i = 0; i < n; ++i) mu += k[i] * alpha[i];
            std::vector<double> v = k;
            for (std::size_t i = 0; i < n; ++i) {
                double s = v[i];
                for (std::size_t t = 0; t < i; ++t) s -= K[i * n + t] * v[t];
                v[i] = s / K[i * n + i];
            }
            double kk = 0;
            for (std::size_t i = 0; i < n; ++i) kk += v[i] * v[i];
            const double var = std::max(1e-12, sig2 - kk);
            const double sd = std::sqrt(var);
            const double z = (mu - best) / sd;
            const double ei = (mu - best) * normal_cdf(z) +
                              sd * std::exp(-0.5 * z * z) / std::sqrt(2.0 * 3.14159265358979323846);
            if (ei > best_ei) {
                best_ei = ei;
                best_x = x;
            }
        }
        auto batch = backend.evaluate({from_unit(best_x, space)}, objective);
        evals.push_back(batch.front());
    }
    return sorted_by_objective(std::move(evals));
}

std::vector<Evaluation> CmaEsOptimizer::optimize(const std::vector<ParamSpec>& space,
                                                 const Objective& objective, std::size_t budget,
                                                 std::uint64_t seed, IEvaluationBackend& backend) {
    if (space.empty()) throw std::invalid_argument("cmaes: empty parameter space");
    const std::size_t n = space.size();
    const auto nd = static_cast<double>(n);
    std::mt19937_64 rng(seed);
    std::normal_distribution<double> gauss(0.0, 1.0);

    const std::size_t lambda = 4 + static_cast<std::size_t>(std::floor(3.0 * std::log(nd)));
    const std::size_t mu = lambda / 2;
    std::vector<double> w(mu);
    double wsum = 0;
    for (std::size_t i = 0; i < mu; ++i) {
        w[i] = std::log(static_cast<double>(mu) + 0.5) - std::log(static_cast<double>(i + 1));
        wsum += w[i];
    }
    double mueff_num = 0, mueff_den = 0;
    for (std::size_t i = 0; i < mu; ++i) {
        w[i] /= wsum;
        mueff_num += w[i];
        mueff_den += w[i] * w[i];
    }
    const double mueff = mueff_num * mueff_num / mueff_den;
    const double cc = (4 + mueff / nd) / (nd + 4 + 2 * mueff / nd);
    const double cs = (mueff + 2) / (nd + mueff + 5);
    const double c1 = 2 / ((nd + 1.3) * (nd + 1.3) + mueff);
    const double cmu =
        std::min(1 - c1, 2 * (mueff - 2 + 1 / mueff) / ((nd + 2) * (nd + 2) + mueff));
    const double damps = 1 + 2 * std::max(0.0, std::sqrt((mueff - 1) / (nd + 1)) - 1) + cs;
    const double chiN = std::sqrt(nd) * (1 - 1 / (4 * nd) + 1 / (21 * nd * nd));

    std::vector<double> mean(n, 0.5), pc(n, 0.0), ps(n, 0.0);
    std::vector<double> C(n * n, 0.0), B(n * n, 0.0), D(n, 1.0);
    for (std::size_t i = 0; i < n; ++i) {
        C[i * n + i] = 1.0;
        B[i * n + i] = 1.0;
    }
    double sigma = sigma0_;

    std::vector<Evaluation> all;
    std::size_t eig_iter = 0;
    while (all.size() < budget) {
        std::vector<std::vector<double>> zs(lambda, std::vector<double>(n));
        std::vector<std::vector<double>> xs(lambda, std::vector<double>(n));
        std::vector<ParamMap> cand(lambda);
        for (std::size_t k = 0; k < lambda; ++k) {
            for (std::size_t i = 0; i < n; ++i) zs[k][i] = gauss(rng);
            for (std::size_t i = 0; i < n; ++i) {
                double s = 0;
                for (std::size_t j = 0; j < n; ++j) s += B[i * n + j] * D[j] * zs[k][j];
                xs[k][i] = std::clamp(mean[i] + sigma * s, 0.0, 1.0);
            }
            cand[k] = from_unit(xs[k], space);
        }
        auto evals = backend.evaluate(cand, objective);
        std::vector<std::size_t> order(lambda);
        for (std::size_t i = 0; i < lambda; ++i) order[i] = i;
        std::stable_sort(order.begin(), order.end(), [&evals](std::size_t a, std::size_t b) {
            return evals[a].objective > evals[b].objective;
        });
        for (const auto& e : evals) {
            all.push_back(e);
            if (all.size() >= budget) break;
        }
        if (all.size() >= budget) break;

        std::vector<double> old_mean = mean;
        std::fill(mean.begin(), mean.end(), 0.0);
        for (std::size_t r = 0; r < mu; ++r)
            for (std::size_t i = 0; i < n; ++i) mean[i] += w[r] * xs[order[r]][i];

        std::vector<double> y(n), zmean(n, 0.0);
        for (std::size_t i = 0; i < n; ++i) y[i] = (mean[i] - old_mean[i]) / sigma;
        for (std::size_t r = 0; r < mu; ++r)
            for (std::size_t i = 0; i < n; ++i) zmean[i] += w[r] * zs[order[r]][i];
        for (std::size_t i = 0; i < n; ++i) {
            double bz = 0;
            for (std::size_t j = 0; j < n; ++j) bz += B[i * n + j] * zmean[j];
            ps[i] = (1 - cs) * ps[i] + std::sqrt(cs * (2 - cs) * mueff) * bz;
        }
        double psn = 0;
        for (double v : ps) psn += v * v;
        psn = std::sqrt(psn);
        const bool hsig = psn / std::sqrt(1 - std::pow(1 - cs, 2.0 * (eig_iter + 1))) / chiN <
                          1.4 + 2.0 / (nd + 1);
        for (std::size_t i = 0; i < n; ++i)
            pc[i] = (1 - cc) * pc[i] + (hsig ? std::sqrt(cc * (2 - cc) * mueff) * y[i] : 0.0);

        for (std::size_t i = 0; i < n; ++i) {
            for (std::size_t j = 0; j < n; ++j) {
                double rank_mu = 0;
                for (std::size_t r = 0; r < mu; ++r) {
                    double yi = (xs[order[r]][i] - old_mean[i]) / sigma;
                    double yj = (xs[order[r]][j] - old_mean[j]) / sigma;
                    rank_mu += w[r] * yi * yj;
                }
                C[i * n + j] = (1 - c1 - cmu) * C[i * n + j] + c1 * pc[i] * pc[j] + cmu * rank_mu;
            }
        }
        sigma *= std::exp((cs / damps) * (psn / chiN - 1));
        sigma = std::clamp(sigma, 1e-8, 1.0);

        ++eig_iter;
        std::vector<double> Bn(n * n, 0.0);
        std::vector<double> An = C;
        for (std::size_t i = 0; i < n; ++i) Bn[i * n + i] = 1.0;
        for (std::size_t sweep = 0; sweep < 48; ++sweep) {
            double off = 0;
            std::size_t p = 0, q = 1;
            for (std::size_t i = 0; i < n; ++i)
                for (std::size_t j = i + 1; j < n; ++j)
                    if (std::fabs(An[i * n + j]) > off) {
                        off = std::fabs(An[i * n + j]);
                        p = i;
                        q = j;
                    }
            if (off < 1e-12 || n < 2) break;
            const double app = An[p * n + p], aqq = An[q * n + q], apq = An[p * n + q];
            const double theta = 0.5 * std::atan2(2 * apq, aqq - app);
            const double c = std::cos(theta), s = std::sin(theta);
            for (std::size_t i = 0; i < n; ++i) {
                const double aip = An[i * n + p], aiq = An[i * n + q];
                An[i * n + p] = c * aip - s * aiq;
                An[i * n + q] = s * aip + c * aiq;
                const double bip = Bn[i * n + p], biq = Bn[i * n + q];
                Bn[i * n + p] = c * bip - s * biq;
                Bn[i * n + q] = s * bip + c * biq;
            }
            for (std::size_t j = 0; j < n; ++j) {
                const double apj = An[p * n + j], aqj = An[q * n + j];
                An[p * n + j] = c * apj - s * aqj;
                An[q * n + j] = s * apj + c * aqj;
            }
        }
        B = Bn;
        for (std::size_t i = 0; i < n; ++i) D[i] = std::sqrt(std::max(1e-14, An[i * n + i]));
    }
    return sorted_by_objective(std::move(all));
}

}
