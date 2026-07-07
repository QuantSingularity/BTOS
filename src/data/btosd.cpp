#include "btos/data/btosd.hpp"

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstring>
#include <fstream>
#include <stdexcept>

using namespace std;

namespace btos {
namespace {

constexpr char kMagic[8] = {'B', 'T', 'O', 'S', 'D', '1', '\0', '\0'};
constexpr std::uint32_t kKindBars = 1;
constexpr std::uint32_t kKindTrades = 2;

struct Header {
    char magic[8];
    std::uint32_t kind;
    std::uint32_t reserved;
    std::uint64_t count;
    std::int64_t period_ns;
};

template <typename T>
void write_column(std::ofstream& out, const std::vector<T>& col) {
    out.write(reinterpret_cast<const char*>(col.data()),
              static_cast<std::streamsize>(col.size() * sizeof(T)));
}

template <typename T>
const T* column_at(const MappedFile& map, std::size_t& offset, std::size_t count) {
    if (offset + count * sizeof(T) > map.size())
        throw std::runtime_error("btosd: truncated column data");
    const T* p = reinterpret_cast<const T*>(map.data() + offset);
    offset += count * sizeof(T);
    return p;
}

Header read_header(const MappedFile& map, std::uint32_t expect_kind) {
    if (map.size() < sizeof(Header)) throw std::runtime_error("btosd: file too small");
    Header h;
    std::memcpy(&h, map.data(), sizeof(Header));
    if (std::memcmp(h.magic, kMagic, sizeof(kMagic)) != 0)
        throw std::runtime_error("btosd: bad magic");
    if (h.kind != expect_kind) throw std::runtime_error("btosd: unexpected dataset kind");
    return h;
}

}

MappedFile::MappedFile(const std::string& path) {
    const int fd = ::open(path.c_str(), O_RDONLY);
    if (fd < 0) throw std::runtime_error("btosd: cannot open " + path);
    struct stat st{};
    if (::fstat(fd, &st) != 0) {
        ::close(fd);
        throw std::runtime_error("btosd: cannot stat " + path);
    }
    size_ = static_cast<std::size_t>(st.st_size);
    if (size_ > 0) {
        void* p = ::mmap(nullptr, size_, PROT_READ, MAP_PRIVATE, fd, 0);
        if (p == MAP_FAILED) {
            ::close(fd);
            throw std::runtime_error("btosd: mmap failed for " + path);
        }
        data_ = static_cast<const std::byte*>(p);
    }
    ::close(fd);
}

MappedFile::~MappedFile() {
    if (data_ != nullptr) ::munmap(const_cast<std::byte*>(data_), size_);
}

MappedFile::MappedFile(MappedFile&& other) noexcept : data_(other.data_), size_(other.size_) {
    other.data_ = nullptr;
    other.size_ = 0;
}

void write_btosd(const std::string& path, const std::vector<Bar>& bars, DurationNs period) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) throw std::runtime_error("btosd: cannot write " + path);
    Header h{};
    std::memcpy(h.magic, kMagic, sizeof(kMagic));
    h.kind = kKindBars;
    h.count = bars.size();
    h.period_ns = period.ns;
    out.write(reinterpret_cast<const char*>(&h), sizeof(h));
    const std::size_t n = bars.size();
    std::vector<std::int64_t> ts(n);
    std::vector<double> o(n), hi(n), lo(n), c(n), v(n);
    for (std::size_t i = 0; i < n; ++i) {
        ts[i] = bars[i].ts.ns;
        o[i] = bars[i].open;
        hi[i] = bars[i].high;
        lo[i] = bars[i].low;
        c[i] = bars[i].close;
        v[i] = bars[i].volume;
    }
    write_column(out, ts);
    write_column(out, o);
    write_column(out, hi);
    write_column(out, lo);
    write_column(out, c);
    write_column(out, v);
    if (!out) throw std::runtime_error("btosd: write failed for " + path);
}

void write_btosd(const std::string& path, const std::vector<Trade>& trades) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) throw std::runtime_error("btosd: cannot write " + path);
    Header h{};
    std::memcpy(h.magic, kMagic, sizeof(kMagic));
    h.kind = kKindTrades;
    h.count = trades.size();
    h.period_ns = 0;
    out.write(reinterpret_cast<const char*>(&h), sizeof(h));
    const std::size_t n = trades.size();
    std::vector<std::int64_t> ts(n);
    std::vector<double> px(n), sz(n);
    std::vector<std::uint8_t> ag(n);
    for (std::size_t i = 0; i < n; ++i) {
        ts[i] = trades[i].ts.ns;
        px[i] = trades[i].price;
        sz[i] = trades[i].size;
        ag[i] = static_cast<std::uint8_t>(trades[i].aggressor);
    }
    write_column(out, ts);
    write_column(out, px);
    write_column(out, sz);
    write_column(out, ag);
    if (!out) throw std::runtime_error("btosd: write failed for " + path);
}

BtosdBarReader::BtosdBarReader(const std::string& path) : map_(path) {
    const Header h = read_header(map_, kKindBars);
    period_ = DurationNs{h.period_ns};
    const auto n = static_cast<std::size_t>(h.count);
    std::size_t off = sizeof(Header);
    const auto* ts = column_at<std::int64_t>(map_, off, n);
    const auto* o = column_at<double>(map_, off, n);
    const auto* hi = column_at<double>(map_, off, n);
    const auto* lo = column_at<double>(map_, off, n);
    const auto* c = column_at<double>(map_, off, n);
    const auto* v = column_at<double>(map_, off, n);
    bars_.resize(n);
    for (std::size_t i = 0; i < n; ++i)
        bars_[i] = Bar{TimestampNs{ts[i]}, o[i], hi[i], lo[i], c[i], v[i]};
}

BtosdTickReader::BtosdTickReader(const std::string& path) : map_(path) {
    const Header h = read_header(map_, kKindTrades);
    const auto n = static_cast<std::size_t>(h.count);
    std::size_t off = sizeof(Header);
    const auto* ts = column_at<std::int64_t>(map_, off, n);
    const auto* px = column_at<double>(map_, off, n);
    const auto* sz = column_at<double>(map_, off, n);
    const auto* ag = column_at<std::uint8_t>(map_, off, n);
    trades_.resize(n);
    for (std::size_t i = 0; i < n; ++i)
        trades_[i] = Trade{TimestampNs{ts[i]}, px[i], sz[i], static_cast<Side>(ag[i])};
}

}
