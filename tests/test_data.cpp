#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

#include "btos/data/btosd.hpp"
#include "btos/data/calendar.hpp"
#include "btos/data/corporate_actions.hpp"
#include "btos/data/instrument_master.hpp"
#include "btos/data/readers.hpp"
#include "btos/data/synthetic.hpp"
#include "btos/data/validate.hpp"

using namespace std;
using namespace btos;

namespace {

string write_temp(const string& name, const string& content) {
    const auto path = (filesystem::temp_directory_path() / name).string();
    ofstream out(path, ios::trunc);
    out << content;
    return path;
}

vector<Bar> sample_bars(size_t n, TimestampNs start, DurationNs period, double px = 100.0) {
    vector<Bar> bars(n);
    TimestampNs ts = start;
    for (size_t i = 0; i < n; ++i) {
        ts = ts + period;
        bars[i] = Bar{ts, px, px + 1, px - 1, px + 0.5, 1'000.0};
        px += 0.5;
    }
    return bars;
}

}

TEST(InstrumentMaster, FromCsvAssignsIdsAndParsesFields) {
    const auto path = write_temp("btos_inst.csv",
                                 "symbol,asset_class,currency,tick_size,multiplier,lot_size,listing,delisting\n"
                                 "AAA,equity,USD,0.01,1,1,2010-01-01,\n"
                                 "BBB,future,EUR,0.25,50,1,2015-06-01,2022-01-01\n");
    auto m = InstrumentMaster::from_csv(path);
    ASSERT_EQ(m.all().size(), 2u);
    const auto a = m.find("AAA");
    ASSERT_TRUE(a.has_value());
    EXPECT_EQ(m.get(*a).currency, "USD");
    const auto b = m.find("BBB");
    ASSERT_TRUE(b.has_value());
    EXPECT_EQ(m.get(*b).multiplier, 50);
    EXPECT_EQ(m.get(*b).asset_class, AssetClass::Future);
    filesystem::remove(path);
}

TEST(InstrumentMaster, InvariantSurvivorshipDelistedExcludedFromUniverse) {
    InstrumentMaster m;
    Instrument alive;
    alive.id = InstrumentId{1};
    alive.symbol = "LIVE";
    alive.listing = parse_iso8601_utc("2010-01-01");
    alive.delisting = kMaxTimestamp;
    Instrument dead = alive;
    dead.id = InstrumentId{2};
    dead.symbol = "DEAD";
    dead.delisting = parse_iso8601_utc("2020-01-01");
    m.add(alive);
    m.add(dead);
    const auto before = m.active_universe(parse_iso8601_utc("2019-06-01"));
    const auto after = m.active_universe(parse_iso8601_utc("2020-06-01"));
    EXPECT_EQ(before.size(), 2u);
    ASSERT_EQ(after.size(), 1u);
    EXPECT_EQ(after[0].value, 1u);
}

TEST(Calendar, AlwaysOpenIsAlwaysOpen) {
    const auto cal = Calendar::always_open();
    EXPECT_TRUE(cal.is_open(parse_iso8601_utc("2021-01-03T03:00:00Z")));
}

TEST(Calendar, WeekdaySessionRespectsHoursWeekendsHolidays) {
    auto cal = Calendar::weekday_session(hours(14), hours(21));
    EXPECT_TRUE(cal.is_open(parse_iso8601_utc("2021-03-05T15:00:00Z")));
    EXPECT_FALSE(cal.is_open(parse_iso8601_utc("2021-03-05T10:00:00Z")));
    EXPECT_FALSE(cal.is_open(parse_iso8601_utc("2021-03-06T15:00:00Z")));
    const auto holiday = parse_iso8601_utc("2021-03-04T15:00:00Z");
    EXPECT_TRUE(cal.is_open(holiday));
    cal.add_holiday(utc_day_index(holiday));
    EXPECT_FALSE(cal.is_open(holiday));
    const auto reopened = cal.next_open(holiday);
    EXPECT_EQ(format_iso8601_utc(reopened).substr(0, 13), "2021-03-05T14");
}

TEST(Readers, CsvBarReaderParses) {
    const auto path = write_temp("btos_bars.csv",
                                 "ts,open,high,low,close,volume\n"
                                 "2021-01-01T00:00:00Z,100,101,99,100.5,5000\n"
                                 "2021-01-02T00:00:00Z,100.5,102,100,101.5,6000\n");
    CsvBarReader r(path, days(1));
    ASSERT_EQ(r.bars().size(), 2u);
    EXPECT_DOUBLE_EQ(r.bars()[1].close, 101.5);
    filesystem::remove(path);
}

TEST(Readers, InvariantPointInTimeBlocksFutureBars) {
    MemoryBarReader reader(sample_bars(10, TimestampNs{0}, days(1)), days(1));
    PointInTimeBars view(reader);
    view.advance_to(reader.bars()[4].ts);
    EXPECT_EQ(view.size(), 5u);
    EXPECT_NO_THROW(static_cast<void>(view.at(4)));
    EXPECT_THROW(static_cast<void>(view.at(5)), PointInTimeBars::LookaheadViolation);
    EXPECT_DOUBLE_EQ(view.latest().close, reader.bars()[4].close);
    view.advance_to(reader.bars()[9].ts);
    EXPECT_EQ(view.size(), 10u);
    EXPECT_THROW(static_cast<void>(view.at(11)), out_of_range);
}

TEST(Btosd, BarRoundTripThroughMmap) {
    const auto path = (filesystem::temp_directory_path() / "btos_test.btosd").string();
    const auto bars = sample_bars(100, parse_iso8601_utc("2020-01-01"), days(1));
    write_btosd(path, bars, days(1));
    BtosdBarReader r(path);
    ASSERT_EQ(r.bars().size(), bars.size());
    EXPECT_EQ(r.period().ns, days(1).ns);
    for (size_t i = 0; i < bars.size(); i += 17) {
        EXPECT_EQ(r.bars()[i].ts.ns, bars[i].ts.ns);
        EXPECT_DOUBLE_EQ(r.bars()[i].close, bars[i].close);
    }
    filesystem::remove(path);
}

TEST(Btosd, TradeRoundTripAndBadMagicRejected) {
    const auto path = (filesystem::temp_directory_path() / "btos_trades.btosd").string();
    vector<Trade> trades{{TimestampNs{1}, 10.0, 5.0, Side::Buy}, {TimestampNs{2}, 10.1, 3.0, Side::Sell}};
    write_btosd(path, trades);
    BtosdTickReader r(path);
    ASSERT_EQ(r.trades().size(), 2u);
    EXPECT_EQ(r.trades()[1].aggressor, Side::Sell);
    const auto bad = write_temp("btos_bad.btosd", "not a btosd file at all............");
    EXPECT_THROW(BtosdBarReader{bad}, runtime_error);
    filesystem::remove(path);
    filesystem::remove(bad);
}

TEST(CorporateActions, InvariantPointInTimeAdjustmentNeverRetroactive) {
    CorporateActionBook book;
    CorporateAction split;
    split.instrument = InstrumentId{1};
    split.kind = CorporateAction::Kind::Split;
    split.effective = parse_iso8601_utc("2021-06-01");
    split.ratio = 2.0;
    book.add(split);
    const auto obs = parse_iso8601_utc("2021-05-01");
    const auto before = parse_iso8601_utc("2021-05-15");
    const auto after = parse_iso8601_utc("2021-07-01");
    EXPECT_DOUBLE_EQ(book.adjustment_factor(InstrumentId{1}, obs, before), 1.0);
    EXPECT_DOUBLE_EQ(book.adjustment_factor(InstrumentId{1}, obs, after), 0.5);
    vector<Bar> raw{Bar{obs, 100.0, 100.0, 100.0, 100.0, 1'000.0}};
    const auto adjusted = book.adjust_asof(InstrumentId{1}, raw, after);
    ASSERT_EQ(adjusted.size(), 1u);
    EXPECT_DOUBLE_EQ(adjusted[0].close, 50.0);
    EXPECT_DOUBLE_EQ(adjusted[0].volume, 2'000.0);
    EXPECT_EQ(book.known_asof(InstrumentId{1}, before).size(), 0u);
    EXPECT_EQ(book.known_asof(InstrumentId{1}, after).size(), 1u);
}

TEST(Validation, FlagsAllIssueKinds) {
    vector<Bar> bars = sample_bars(30, TimestampNs{0}, days(1));
    bars[3].high = bars[3].low - 1;
    bars[5].close = -2;
    bars[7].volume = -10;
    bars[9].ts = bars[8].ts;
    bars[11].ts = bars[10].ts - days(1);
    const auto issues = validate_bars(bars, days(1), Calendar::always_open());
    auto has = [&issues](ValidationIssue::Kind k) {
        for (const auto& i : issues)
            if (i.kind == k) return true;
        return false;
    };
    EXPECT_TRUE(has(ValidationIssue::Kind::OhlcInconsistent));
    EXPECT_TRUE(has(ValidationIssue::Kind::NonPositivePrice));
    EXPECT_TRUE(has(ValidationIssue::Kind::NegativeVolume));
    EXPECT_TRUE(has(ValidationIssue::Kind::DuplicateTimestamp));
    EXPECT_TRUE(has(ValidationIssue::Kind::NonMonotonicTimestamp));
    EXPECT_FALSE(format_issues(issues).empty());
}

TEST(Validation, CleanSeriesPasses) {
    const auto bars = sample_bars(50, TimestampNs{0}, days(1));
    EXPECT_TRUE(validate_bars(bars, days(1), Calendar::always_open()).empty());
}

TEST(Synthetic, GbmIsSeededDeterministicAndValid) {
    mt19937_64 rng1(7), rng2(7), rng3(8);
    GbmParams p;
    const auto a = generate_gbm_bars(rng1, p, parse_iso8601_utc("2020-01-01"), days(1), 500);
    const auto b = generate_gbm_bars(rng2, p, parse_iso8601_utc("2020-01-01"), days(1), 500);
    const auto c = generate_gbm_bars(rng3, p, parse_iso8601_utc("2020-01-01"), days(1), 500);
    ASSERT_EQ(a.size(), 500u);
    EXPECT_DOUBLE_EQ(a[499].close, b[499].close);
    EXPECT_NE(a[499].close, c[499].close);
    EXPECT_TRUE(validate_bars(a, days(1), Calendar::always_open()).empty());
}

TEST(Synthetic, HestonAndPoissonProduceValidSeries) {
    mt19937_64 rng(11);
    HestonParams hp;
    const auto bars = generate_heston_bars(rng, hp, parse_iso8601_utc("2020-01-01"), days(1), 300);
    EXPECT_TRUE(validate_bars(bars, days(1), Calendar::always_open()).empty());
    PoissonTradeParams tp;
    tp.lambda_per_second = 0.001;
    const auto trades = generate_poisson_trades(rng, tp, bars, bars.front().ts, bars.back().ts);
    EXPECT_GT(trades.size(), 10u);
    for (size_t i = 1; i < trades.size(); ++i) EXPECT_GE(trades[i].ts.ns, trades[i - 1].ts.ns);
}
