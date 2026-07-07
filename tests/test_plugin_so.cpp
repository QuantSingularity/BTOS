#include <gtest/gtest.h>

#include <filesystem>
#include <random>

#include "btos/data/synthetic.hpp"
#include "btos/engine.hpp"
#include "btos/plugin_registry.hpp"
#include "btos/strategy/reference.hpp"

using namespace std;
using namespace btos;

#ifdef BTOS_EXAMPLE_PLUGIN_SO

namespace {

InstrumentMaster one_syn() {
    InstrumentMaster m;
    Instrument i;
    i.id = InstrumentId{1};
    i.symbol = "SYN";
    i.listing = TimestampNs{0};
    m.add(i);
    return m;
}

shared_ptr<IBarReader> gbm_feed(uint64_t seed, size_t n) {
    mt19937_64 rng(seed);
    GbmParams p;
    return make_shared<MemoryBarReader>(
        generate_gbm_bars(rng, p, parse_iso8601_utc("2020-01-01"), days(1), n), days(1));
}

}

TEST(PluginSo, LoadsRealSharedLibraryFromDisk) {
    ASSERT_TRUE(filesystem::exists(BTOS_EXAMPLE_PLUGIN_SO))
        << "expected built plugin at " << BTOS_EXAMPLE_PLUGIN_SO;
    PluginRegistry reg;
    reg.load_shared_library(BTOS_EXAMPLE_PLUGIN_SO);
    ASSERT_TRUE(reg.has("spread_slippage"));
    auto model = reg.create_slippage("spread_slippage", "{\"half_spread_bps\": 10.0}");
    SlippageContext buy{100.0, 5.0, Side::Buy, 1'000.0, 0.02};
    SlippageContext sell{100.0, 5.0, Side::Sell, 1'000.0, 0.02};
    EXPECT_NEAR(model->apply(buy), 100.10, 1e-9);
    EXPECT_NEAR(model->apply(sell), 99.90, 1e-9);
}

TEST(PluginSo, DefaultConfigWhenJsonMissingKey) {
    PluginRegistry reg;
    reg.load_shared_library(BTOS_EXAMPLE_PLUGIN_SO);
    auto model = reg.create_slippage("spread_slippage", "{}");
    SlippageContext buy{100.0, 1.0, Side::Buy, 1'000.0, 0.02};
    EXPECT_NEAR(model->apply(buy), 100.05, 1e-9);
}

TEST(PluginSo, DrivesBacktestThroughDlopenedModel) {
    PluginRegistry reg;
    reg.load_shared_library(BTOS_EXAMPLE_PLUGIN_SO);
    Engine eng;
    eng.with_instruments(one_syn())
        .with_bars("SYN", gbm_feed(42, 300))
        .with_strategy(make_unique<MaCrossoverStrategy>("SYN"))
        .with_params({{"fast", 5}, {"slow", 20}, {"quantity", 100}})
        .with_slippage(reg.create_slippage("spread_slippage", "{\"half_spread_bps\": 8.0}"))
        .with_seed(42);
    const RunResult r = eng.run();
    EXPECT_GT(r.fills.size(), 0u);
    EXPECT_TRUE(isfinite(r.final_equity));
}

#endif
