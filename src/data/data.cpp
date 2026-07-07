#include <algorithm>
#include <fstream>
#include <sstream>
#include <stdexcept>

#include "btos/data/calendar.hpp"
#include "btos/data/corporate_actions.hpp"
#include "btos/data/instrument_master.hpp"
#include "btos/data/readers.hpp"

using namespace std;

namespace btos {
namespace {

std::vector<std::string> split_csv_line(const std::string& line) {
    std::vector<std::string> out;
    std::stringstream ss(line);
    std::string field;
    while (std::getline(ss, field, ',')) out.push_back(field);
    if (!line.empty() && line.back() == ',') out.emplace_back();
    return out;
}

std::string strip_cr(std::string s) {
    if (!s.empty() && s.back() == '\r') s.pop_back();
    return s;
}

}

AssetClass parse_asset_class(const std::string& s) {
    if (s == "equity") return AssetClass::Equity;
    if (s == "etf") return AssetClass::ETF;
    if (s == "future") return AssetClass::Future;
    if (s == "fx") return AssetClass::FX;
    if (s == "crypto") return AssetClass::CryptoSpot;
    throw std::invalid_argument("unknown asset class: " + s);
}

void InstrumentMaster::add(Instrument inst) {
    if (index_by_id_.count(inst.id.value) > 0)
        throw std::invalid_argument("duplicate instrument id");
    if (by_symbol_.count(inst.symbol) > 0)
        throw std::invalid_argument("duplicate symbol: " + inst.symbol);
    index_by_id_[inst.id.value] = instruments_.size();
    by_symbol_[inst.symbol] = inst.id;
    instruments_.push_back(std::move(inst));
}

InstrumentMaster InstrumentMaster::from_csv(const std::string& path) {
    std::ifstream in(path);
    if (!in) throw std::runtime_error("instrument master: cannot open " + path);
    InstrumentMaster m;
    std::string line;
    std::getline(in, line);
    std::uint32_t next_id = 1;
    while (std::getline(in, line)) {
        line = strip_cr(line);
        if (line.empty()) continue;
        auto f = split_csv_line(line);
        if (f.size() < 7)
            throw std::runtime_error("instrument master: bad row: " + line);
        Instrument inst;
        inst.id = InstrumentId{next_id++};
        inst.symbol = f[0];
        inst.asset_class = parse_asset_class(f[1]);
        inst.currency = f[2];
        inst.tick_size = std::stod(f[3]);
        inst.multiplier = std::stod(f[4]);
        inst.lot_size = std::stod(f[5]);
        inst.listing = parse_iso8601_utc(f[6]);
        inst.delisting = (f.size() > 7 && !f[7].empty()) ? parse_iso8601_utc(f[7]) : kMaxTimestamp;
        m.add(std::move(inst));
    }
    return m;
}

const Instrument& InstrumentMaster::get(InstrumentId id) const {
    auto it = index_by_id_.find(id.value);
    if (it == index_by_id_.end()) throw std::out_of_range("unknown instrument id");
    return instruments_[it->second];
}

std::optional<InstrumentId> InstrumentMaster::find(const std::string& symbol) const {
    auto it = by_symbol_.find(symbol);
    if (it == by_symbol_.end()) return std::nullopt;
    return it->second;
}

std::vector<InstrumentId> InstrumentMaster::active_universe(TimestampNs t) const {
    std::vector<InstrumentId> out;
    for (const auto& i : instruments_)
        if (i.active_at(t)) out.push_back(i.id);
    return out;
}

Calendar Calendar::always_open() {
    Calendar c;
    for (auto& s : c.week_) s = Session{true, 0, 86'400'000'000'000LL};
    return c;
}

Calendar Calendar::weekday_session(DurationNs open_ns, DurationNs close_ns) {
    if (open_ns.ns >= close_ns.ns) throw std::invalid_argument("calendar: open must precede close");
    Calendar c;
    for (int wd = 0; wd < 5; ++wd)
        c.week_[static_cast<std::size_t>(wd)] = Session{true, open_ns.ns, close_ns.ns};
    return c;
}

const Calendar::Session& Calendar::session_for_day(std::int64_t day) const {

    std::int64_t wd = (day + 3) % 7;
    if (wd < 0) wd += 7;
    return week_[static_cast<std::size_t>(wd)];
}

bool Calendar::is_open(TimestampNs t) const {
    const std::int64_t day = utc_day_index(t);
    if (is_holiday(day)) return false;
    const Session& s = session_for_day(day);
    if (!s.open_day) return false;
    const std::int64_t r = ns_since_midnight(t);
    return r >= s.open_ns && r < s.close_ns;
}

TimestampNs Calendar::next_open(TimestampNs t) const {
    const std::int64_t day_ns = 86'400'000'000'000LL;
    std::int64_t day = utc_day_index(t);
    for (int i = 0; i < 3700; ++i, ++day) {
        if (is_holiday(day)) continue;
        const Session& s = session_for_day(day);
        if (!s.open_day) continue;
        const TimestampNs open{day * day_ns + s.open_ns};
        const TimestampNs close{day * day_ns + s.close_ns};
        if (t < open) return open;
        if (t < close) return t;
    }
    throw std::runtime_error("calendar: no open session found within 10 years");
}

TimestampNs Calendar::next_close(TimestampNs t) const {
    const std::int64_t day_ns = 86'400'000'000'000LL;
    std::int64_t day = utc_day_index(t);
    for (int i = 0; i < 3700; ++i, ++day) {
        if (is_holiday(day)) continue;
        const Session& s = session_for_day(day);
        if (!s.open_day) continue;
        const TimestampNs close{day * day_ns + s.close_ns};
        if (t < close) return close;
    }
    throw std::runtime_error("calendar: no session close found within 10 years");
}

CsvBarReader::CsvBarReader(const std::string& path, DurationNs period) : period_(period) {
    std::ifstream in(path);
    if (!in) throw std::runtime_error("csv reader: cannot open " + path);
    std::string line;
    std::getline(in, line);
    while (std::getline(in, line)) {
        line = strip_cr(line);
        if (line.empty()) continue;
        auto f = split_csv_line(line);
        if (f.size() < 6) throw std::runtime_error("csv reader: bad row: " + line);
        Bar b;
        b.ts = parse_iso8601_utc(f[0]);
        b.open = std::stod(f[1]); b.high = std::stod(f[2]);
        b.low = std::stod(f[3]); b.close = std::stod(f[4]);
        b.volume = std::stod(f[5]);
        bars_.push_back(b);
    }
}

CorporateActionBook CorporateActionBook::from_csv(const std::string& path) {
    std::ifstream in(path);
    if (!in) throw std::runtime_error("corporate actions: cannot open " + path);
    CorporateActionBook book;
    std::string line;
    std::getline(in, line);
    while (std::getline(in, line)) {
        line = strip_cr(line);
        if (line.empty()) continue;
        auto f = split_csv_line(line);
        if (f.size() < 5) throw std::runtime_error("corporate actions: bad row: " + line);
        CorporateAction a;
        a.instrument = InstrumentId{static_cast<std::uint32_t>(std::stoul(f[0]))};
        if (f[1] == "split") a.kind = CorporateAction::Kind::Split;
        else if (f[1] == "dividend") a.kind = CorporateAction::Kind::CashDividend;
        else if (f[1] == "delisting") a.kind = CorporateAction::Kind::Delisting;
        else throw std::runtime_error("corporate actions: unknown kind " + f[1]);
        a.effective = parse_iso8601_utc(f[2]);
        a.ratio = std::stod(f[3]);
        a.amount = std::stod(f[4]);
        book.add(a);
    }
    return book;
}

}
