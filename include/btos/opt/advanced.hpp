#pragma once

#include "btos/opt/optimizer.hpp"

using namespace std;

namespace btos {

class BayesianGpOptimizer final : public IOptimizer {
  public:

    explicit BayesianGpOptimizer(std::size_t init_random = 8) : init_random_(init_random) {}
    std::vector<Evaluation> optimize(const std::vector<ParamSpec>& space, const Objective& objective,
                                     std::size_t budget, std::uint64_t seed,
                                     IEvaluationBackend& backend) override;
    [[nodiscard]] std::string name() const override { return "bayes_gp_ei"; }

  private:
    std::size_t init_random_;
};

class CmaEsOptimizer final : public IOptimizer {
  public:

    explicit CmaEsOptimizer(double sigma0 = 0.3) : sigma0_(sigma0) {}
    std::vector<Evaluation> optimize(const std::vector<ParamSpec>& space, const Objective& objective,
                                     std::size_t budget, std::uint64_t seed,
                                     IEvaluationBackend& backend) override;
    [[nodiscard]] std::string name() const override { return "cmaes"; }

  private:
    double sigma0_;
};

}
