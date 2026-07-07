#pragma once

#include <string>
#include <vector>

#include "btos/data/readers.hpp"

using namespace std;

namespace btos {

class MappedFile {
  public:

    explicit MappedFile(const std::string& path);
    ~MappedFile();
    MappedFile(const MappedFile&) = delete;
    MappedFile& operator=(const MappedFile&) = delete;
    MappedFile(MappedFile&& other) noexcept;
    MappedFile& operator=(MappedFile&&) = delete;

    [[nodiscard]] const std::byte* data() const { return data_; }

    [[nodiscard]] std::size_t size() const { return size_; }

  private:
    const std::byte* data_{nullptr};
    std::size_t size_{0};
};

void write_btosd(const std::string& path, const std::vector<Bar>& bars, DurationNs period);

void write_btosd(const std::string& path, const std::vector<Trade>& trades);

class BtosdBarReader final : public IBarReader {
  public:
    explicit BtosdBarReader(const std::string& path);
    [[nodiscard]] const std::vector<Bar>& bars() const override { return bars_; }
    [[nodiscard]] DurationNs period() const override { return period_; }

  private:
    MappedFile map_;
    std::vector<Bar> bars_;
    DurationNs period_{0};
};

class BtosdTickReader final : public ITickReader {
  public:
    explicit BtosdTickReader(const std::string& path);
    [[nodiscard]] const std::vector<Trade>& trades() const override { return trades_; }

  private:
    MappedFile map_;
    std::vector<Trade> trades_;
};

}
