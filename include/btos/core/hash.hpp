#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

using namespace std;

namespace btos {

class Fnv1a64 {
  public:

    constexpr void update(const void* data, std::size_t n) {
        const auto* p = static_cast<const unsigned char*>(data);
        for (std::size_t i = 0; i < n; ++i) {
            state_ ^= p[i];
            state_ *= 0x100000001b3ULL;
        }
    }

    constexpr void update(std::string_view s) { update(s.data(), s.size()); }

    template <typename T>
    void update_value(const T& v) {
        static_assert(std::is_trivially_copyable_v<T>);
        update(&v, sizeof(T));
    }

    [[nodiscard]] constexpr std::uint64_t digest() const { return state_; }

  private:
    std::uint64_t state_{0xcbf29ce484222325ULL};
};

inline std::uint64_t fnv1a64(std::string_view s) {
    Fnv1a64 h;
    h.update(s);
    return h.digest();
}

std::string to_hex(std::uint64_t v);

}
