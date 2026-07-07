#pragma once

#include <memory>
#include <string>
#include <vector>

#include "btos/data/bar.hpp"

using namespace std;

namespace btos {

class IBarReader {
  public:
    virtual ~IBarReader() = default;

    [[nodiscard]] virtual const std::vector<Bar>& bars() const = 0;

    [[nodiscard]] virtual DurationNs period() const = 0;
};

class ITickReader {
  public:
    virtual ~ITickReader() = default;

    [[nodiscard]] virtual const std::vector<Trade>& trades() const = 0;
};

class MemoryBarReader final : public IBarReader {
  public:
    MemoryBarReader(std::vector<Bar> bars, DurationNs period)
        : bars_(std::move(bars)), period_(period) {}
    [[nodiscard]] const std::vector<Bar>& bars() const override { return bars_; }
    [[nodiscard]] DurationNs period() const override { return period_; }

  private:
    std::vector<Bar> bars_;
    DurationNs period_;
};

class CsvBarReader final : public IBarReader {
  public:
    CsvBarReader(const std::string& path, DurationNs period);
    [[nodiscard]] const std::vector<Bar>& bars() const override { return bars_; }
    [[nodiscard]] DurationNs period() const override { return period_; }

  private:
    std::vector<Bar> bars_;
    DurationNs period_;
};

class PointInTimeBars {
  public:

    struct LookaheadViolation : std::logic_error {
        LookaheadViolation() : std::logic_error("lookahead violation: data beyond simulation time requested") {}
    };

    explicit PointInTimeBars(const IBarReader& reader) : reader_(&reader) {}

    void advance_to(TimestampNs now) {
        const auto& b = reader_->bars();
        while (visible_ < b.size() && b[visible_].ts <= now) ++visible_;
        now_ = now;
    }

    [[nodiscard]] std::size_t size() const { return visible_; }

    [[nodiscard]] const Bar& at(std::size_t i) const {
        const auto& b = reader_->bars();
        if (i >= b.size()) throw std::out_of_range("PointInTimeBars: index beyond dataset");
        if (i >= visible_) throw LookaheadViolation{};
        return b[i];
    }

    [[nodiscard]] const Bar& latest() const {
        if (visible_ == 0) throw std::out_of_range("PointInTimeBars: no visible bars");
        return reader_->bars()[visible_ - 1];
    }

    [[nodiscard]] TimestampNs now() const { return now_; }

  private:
    const IBarReader* reader_;
    std::size_t visible_{0};
    TimestampNs now_{kNoTimestamp};
};

}
