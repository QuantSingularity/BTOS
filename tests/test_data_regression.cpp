#include <gtest/gtest.h>

#include <filesystem>

#include "btos/data/calendar.hpp"
#include "btos/data/corporate_actions.hpp"
#include "btos/data/readers.hpp"
#include "btos/data/validate.hpp"

using namespace std;
using namespace btos;

#ifdef BTOS_FIXTURE_DIR

namespace {

string fixture(const string& name) {
    return (filesystem::path(BTOS_FIXTURE_DIR) / name).string();
}

bool has_kind(const vector<ValidationIssue>& issues, ValidationIssue::Kind k) {
    for (const auto& i : issues)
        if (i.kind == k) return true;
    return false;
}

size_t count_kind(const vector<ValidationIssue>& issues, ValidationIssue::Kind k) {
    size_t n = 0;
    for (const auto& i : issues)
        if (i.kind == k) ++n;
    return n;
}

}

TEST(DataRegression, MessyDailyCsvParsesTenBars) {
    const auto path = fixture("messy_daily.csv");
    ASSERT_TRUE(filesystem::exists(path)) << "missing fixture " << path;
    CsvBarReader reader(path, days(1));
    ASSERT_EQ(reader.bars().size(), 10u);
    EXPECT_DOUBLE_EQ(reader.bars().front().close, 100.8);
    EXPECT_DOUBLE_EQ(reader.bars().back().close, 105.7);
    for (size_t i = 1; i < reader.bars().size(); ++i)
        EXPECT_GT(reader.bars()[i].ts.ns, reader.bars()[i - 1].ts.ns);
}

TEST(DataRegression, WeekdayCalendarFlagsGapsButNotWeekends) {
    const auto path = fixture("messy_daily.csv");
    CsvBarReader reader(path, days(1));
    auto cal = Calendar::weekday_session(hours(0), hours(24));
    ValidationOptions opts;
    opts.check_gaps = true;
    const auto issues = validate_bars(reader.bars(), days(1), cal, opts);
    EXPECT_TRUE(has_kind(issues, ValidationIssue::Kind::Gap));
    EXPECT_GE(count_kind(issues, ValidationIssue::Kind::Gap), 1u);
    EXPECT_FALSE(has_kind(issues, ValidationIssue::Kind::NonMonotonicTimestamp));
    EXPECT_FALSE(has_kind(issues, ValidationIssue::Kind::DuplicateTimestamp));
}

TEST(DataRegression, OutlierCheckSuppressedBelowMinimumSample) {
    const auto path = fixture("messy_daily.csv");
    CsvBarReader reader(path, days(1));
    ASSERT_LT(reader.bars().size(), 21u);
    auto cal = Calendar::weekday_session(hours(0), hours(24));
    ValidationOptions opts;
    opts.outlier_z = 2.5;
    opts.check_gaps = false;
    const auto issues = validate_bars(reader.bars(), days(1), cal, opts);
    EXPECT_FALSE(has_kind(issues, ValidationIssue::Kind::ReturnOutlier));
}

TEST(DataRegression, OutlierSpikeDetectedWithEnoughSamples) {
    CsvBarReader reader(fixture("messy_daily.csv"), days(1));
    vector<Bar> bars = reader.bars();
    Bar tail = bars.back();
    while (bars.size() < 40) {
        tail.ts = tail.ts + days(1);
        tail.open = tail.close;
        tail.high = tail.close * 1.002;
        tail.low = tail.close * 0.998;
        tail.close = tail.close * 1.0005;
        bars.push_back(tail);
    }
    auto cal = Calendar::always_open();
    ValidationOptions opts;
    opts.outlier_z = 3.0;
    opts.check_gaps = false;
    const auto issues = validate_bars(bars, days(1), cal, opts);
    EXPECT_TRUE(has_kind(issues, ValidationIssue::Kind::ReturnOutlier));
}

TEST(DataRegression, SplitAppliedPointInTimeToFixtureBars) {
    const auto path = fixture("messy_daily.csv");
    CsvBarReader reader(path, days(1));
    CorporateActionBook book;
    CorporateAction split;
    split.instrument = InstrumentId{1};
    split.kind = CorporateAction::Kind::Split;
    split.effective = parse_iso8601_utc("2021-01-13");
    split.ratio = 2.0;
    book.add(split);
    const auto adjusted = book.adjust_asof(InstrumentId{1}, reader.bars(),
                                           parse_iso8601_utc("2021-01-20"));
    ASSERT_EQ(adjusted.size(), reader.bars().size());
    EXPECT_DOUBLE_EQ(adjusted.front().close, reader.bars().front().close * 0.5);
    const auto raw_asof = book.adjust_asof(InstrumentId{1}, reader.bars(),
                                           parse_iso8601_utc("2021-01-10"));
    EXPECT_DOUBLE_EQ(raw_asof.front().close, reader.bars().front().close);
}

#endif
