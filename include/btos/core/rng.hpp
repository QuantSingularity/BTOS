#pragma once

#include <cstdint>
#include <random>
#include <string_view>

#include "btos/core/hash.hpp"

using namespace std;

namespace btos {

class RngProvider {
  public:

    explicit RngProvider(std::uint64_t master_seed) : master_(master_seed) {}

    [[nodiscard]] std::mt19937_64 stream(std::string_view component) const {
        Fnv1a64 h;
        h.update_value(master_);
        h.update(component);
        return std::mt19937_64{h.digest()};
    }

    [[nodiscard]] std::uint64_t master_seed() const { return master_; }

  private:
    std::uint64_t master_;
};

}
