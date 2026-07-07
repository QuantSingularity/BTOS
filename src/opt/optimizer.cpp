#include "btos/opt/optimizer.hpp"

#include <algorithm>
#include <cmath>
#include <random>
#include <stdexcept>

using namespace std;

namespace btos {

std::vector<Evaluation> SerialBackend::evaluate(const std::vector<ParamMap>& candidates,
                                                const Objective& objective) {
    std::vector<Evaluation> out;
    out.reserve(candidates.size());
    for (const auto& c : candidates) out.push_back(Evaluation{c, objective(c)});
    return out;
}

std::vector<Evaluation> ThreadPoolBackend::evaluate(const std::vector<ParamMap>& candidates,
                                                    const Objective& objective) {
    std::vector<Evaluation> out(candidates.size());
    {
        ThreadPool pool(threads_, std::max<std::size_t>(4, candidates.size()));
        for (std::size_t i = 0; i < candidates.size(); ++i) {
            pool.submit([&out, &candidates, &objective, i] {
                out[i] = Evaluation{candidates[i], objective(candidates[i])};
            });
        }
        pool.wait_idle();
    }
    return out;
}

std::vector<Evaluation> sorted_by_objective(std::vector<Evaluation> evals) {
    std::stable_sort(evals.begin(), evals.end(),
                     [](const Evaluation& a, const Evaluation& b) { return a.objective < b.objective; });
    return evals;
}

std::vector<Evaluation> GridOptimizer::optimize(const std::vector<ParamSpec>& space,
                                                const Objective& objective, std::size_t budget,
                                                std::uint64_t seed, IEvaluationBackend& backend) {
    (void)seed;
    if (space.empty()) throw std::invalid_argument("grid: empty parameter space");
    std::vector<std::vector<double>> axes;
    for (const auto& p : space) {
        std::vector<double> axis;
        if (p.step > 0) {
            for (double v = p.lo; v <= p.hi + 1e-12; v += p.step) axis.push_back(v);
        } else {
            const auto k = static_cast<std::size_t>(std::max(
                2.0, std::ceil(std::pow(static_cast<double>(budget),
                                        1.0 / static_cast<double>(space.size())))));
            for (std::size_t i = 0; i < k; ++i)
                axis.push_back(p.lo + (p.hi - p.lo) * static_cast<double>(i) /
                                          static_cast<double>(k - 1));
        }
        axes.push_back(std::move(axis));
    }
    std::vector<ParamMap> candidates;
    std::vector<std::size_t> idx(space.size(), 0);
    while (candidates.size() < budget) {
        ParamMap m;
        for (std::size_t d = 0; d < space.size(); ++d) m[space[d].name] = axes[d][idx[d]];
        candidates.push_back(std::move(m));
        std::size_t d = 0;
        while (d < space.size()) {
            if (++idx[d] < axes[d].size()) break;
            idx[d] = 0;
            ++d;
        }
        if (d == space.size()) break;
    }
    return sorted_by_objective(backend.evaluate(candidates, objective));
}

std::vector<Evaluation> RandomOptimizer::optimize(const std::vector<ParamSpec>& space,
                                                  const Objective& objective, std::size_t budget,
                                                  std::uint64_t seed, IEvaluationBackend& backend) {
    if (space.empty()) throw std::invalid_argument("random: empty parameter space");
    std::mt19937_64 rng(seed);
    std::uniform_real_distribution<double> u(0.0, 1.0);
    std::vector<ParamMap> candidates;
    candidates.reserve(budget);
    for (std::size_t i = 0; i < budget; ++i) {
        ParamMap m;
        for (const auto& p : space) {
            double v = p.lo + (p.hi - p.lo) * u(rng);
            if (p.step > 0) v = p.lo + std::round((v - p.lo) / p.step) * p.step;
            m[p.name] = std::min(v, p.hi);
        }
        candidates.push_back(std::move(m));
    }
    return sorted_by_objective(backend.evaluate(candidates, objective));
}

}
