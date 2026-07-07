#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "btos/core/thread_pool.hpp"

using namespace std;

namespace btos {

struct ParamSpec {
    std::string name;
    double lo{0}, hi{1};
    double step{0};
};

using ParamMap = std::map<std::string, double>;

struct Evaluation {
    ParamMap params;
    double objective{0};
};

using Objective = std::function<double(const ParamMap&)>;

class IEvaluationBackend {
  public:
    virtual ~IEvaluationBackend() = default;

    virtual std::vector<Evaluation> evaluate(const std::vector<ParamMap>& candidates,
                                             const Objective& objective) = 0;
};

class SerialBackend final : public IEvaluationBackend {
  public:
    std::vector<Evaluation> evaluate(const std::vector<ParamMap>& candidates,
                                     const Objective& objective) override;
};

class ThreadPoolBackend final : public IEvaluationBackend {
  public:
    explicit ThreadPoolBackend(unsigned threads) : threads_(threads) {}
    std::vector<Evaluation> evaluate(const std::vector<ParamMap>& candidates,
                                     const Objective& objective) override;

  private:
    unsigned threads_;
};

class IOptimizer {
  public:
    virtual ~IOptimizer() = default;

    virtual std::vector<Evaluation> optimize(const std::vector<ParamSpec>& space,
                                             const Objective& objective, std::size_t budget,
                                             std::uint64_t seed, IEvaluationBackend& backend) = 0;

    [[nodiscard]] virtual std::string name() const = 0;
};

class GridOptimizer final : public IOptimizer {
  public:
    std::vector<Evaluation> optimize(const std::vector<ParamSpec>& space, const Objective& objective,
                                     std::size_t budget, std::uint64_t seed,
                                     IEvaluationBackend& backend) override;
    [[nodiscard]] std::string name() const override { return "grid"; }
};

class RandomOptimizer final : public IOptimizer {
  public:
    std::vector<Evaluation> optimize(const std::vector<ParamSpec>& space, const Objective& objective,
                                     std::size_t budget, std::uint64_t seed,
                                     IEvaluationBackend& backend) override;
    [[nodiscard]] std::string name() const override { return "random"; }
};

std::vector<Evaluation> sorted_by_objective(std::vector<Evaluation> evals);

}
