#include <gtest/gtest.h>

#include "btos/core/kernel.hpp"
#include "btos/exec/matching_engine.hpp"
#include "btos/exec/order_book.hpp"
#include "btos/portfolio/portfolio.hpp"

using namespace std;
using namespace btos;

namespace {

InstrumentMaster one_equity(const string& symbol = "SYN", const string& ccy = "USD",
                            double multiplier = 1.0) {
    InstrumentMaster m;
    Instrument i;
    i.id = InstrumentId{1};
    i.symbol = symbol;
    i.currency = ccy;
    i.multiplier = multiplier;
    i.listing = TimestampNs{0};
    m.add(i);
    return m;
}

Order make_order(uint64_t id, Side side, OrderType type, double qty, double limit = 0,
                 double stop = 0, TimeInForce tif = TimeInForce::Day) {
    Order o;
    o.id = OrderId{id};
    o.instrument = InstrumentId{1};
    o.side = side;
    o.type = type;
    o.quantity = qty;
    o.limit_price = limit;
    o.stop_price = stop;
    o.tif = tif;
    return o;
}

struct SimHarness {
    Kernel kernel;
    InstrumentMaster master = one_equity();
    BarExchangeSim sim{kernel, master, make_unique<FixedLatency>(milliseconds(1)),
                       make_unique<FixedBpsSlippage>(0.0)};
    vector<FillEvent> fills;
    vector<OrderAckEvent> acks;
    vector<CancelAckEvent> cancels;

    SimHarness() {
        kernel.subscribe([this](const Event& ev) {
            sim.on_event(ev);
            if (const auto* f = get_if<FillEvent>(&ev.payload)) fills.push_back(*f);
            if (const auto* a = get_if<OrderAckEvent>(&ev.payload)) acks.push_back(*a);
            if (const auto* c = get_if<CancelAckEvent>(&ev.payload)) cancels.push_back(*c);
        });
    }

    void bar(int64_t sec, double open, double high, double low, double close, double vol) {
        BarEvent b;
        b.instrument = InstrumentId{1};
        b.open = open;
        b.high = high;
        b.low = low;
        b.close = close;
        b.volume = vol;
        b.period = seconds(60);
        kernel.schedule(TimestampNs{sec * 1'000'000'000LL}, EventPriority::MarketData, b);
    }

    void run(int64_t until_sec) { kernel.run(TimestampNs{until_sec * 1'000'000'000LL}); }
};

}

TEST(OrderBook, PriceTimePriorityAndPartialFills) {
    OrderBook book(0.01);
    book.submit_limit(OrderId{1}, Side::Sell, to_ticks(10.00, 0.01), 5);
    book.submit_limit(OrderId{2}, Side::Sell, to_ticks(10.00, 0.01), 5);
    book.submit_limit(OrderId{3}, Side::Sell, to_ticks(9.99, 0.01), 3);
    const auto fills = book.submit_limit(OrderId{4}, Side::Buy, to_ticks(10.00, 0.01), 10);
    ASSERT_EQ(fills.size(), 3u);
    EXPECT_EQ(fills[0].maker.value, 3u);
    EXPECT_EQ(fills[0].price, to_ticks(9.99, 0.01));
    EXPECT_EQ(fills[1].maker.value, 1u);
    EXPECT_DOUBLE_EQ(fills[1].quantity, 5.0);
    EXPECT_EQ(fills[2].maker.value, 2u);
    EXPECT_DOUBLE_EQ(fills[2].quantity, 2.0);
    EXPECT_DOUBLE_EQ(book.depth_at(Side::Sell, to_ticks(10.00, 0.01)), 3.0);
}

TEST(OrderBook, BestBidNeverCrossesBestAsk) {
    OrderBook book(0.01);
    book.submit_limit(OrderId{1}, Side::Buy, to_ticks(9.98, 0.01), 10);
    book.submit_limit(OrderId{2}, Side::Sell, to_ticks(10.02, 0.01), 10);
    book.submit_limit(OrderId{3}, Side::Buy, to_ticks(10.05, 0.01), 4);
    ASSERT_TRUE(book.best_bid().has_value());
    ASSERT_TRUE(book.best_ask().has_value());
    EXPECT_LT(*book.best_bid(), *book.best_ask());
}

TEST(OrderBook, MarketOrderReportsUnfilled) {
    OrderBook book(0.01);
    book.submit_limit(OrderId{1}, Side::Sell, to_ticks(10.0, 0.01), 4);
    double unfilled = 0;
    const auto fills = book.submit_market(OrderId{2}, Side::Buy, 10, &unfilled);
    ASSERT_EQ(fills.size(), 1u);
    EXPECT_DOUBLE_EQ(fills[0].quantity, 4.0);
    EXPECT_DOUBLE_EQ(unfilled, 6.0);
}

TEST(OrderBook, CancelRemovesExactOrder) {
    OrderBook book(0.01);
    book.submit_limit(OrderId{1}, Side::Buy, to_ticks(9.99, 0.01), 7);
    const auto removed = book.cancel(OrderId{1});
    ASSERT_TRUE(removed.has_value());
    EXPECT_DOUBLE_EQ(*removed, 7.0);
    EXPECT_FALSE(book.best_bid().has_value());
    EXPECT_FALSE(book.cancel(OrderId{1}).has_value());
}

TEST(OrderBook, FokFeasibilityCheck) {
    OrderBook book(0.01);
    book.submit_limit(OrderId{1}, Side::Sell, to_ticks(10.0, 0.01), 4);
    book.submit_limit(OrderId{2}, Side::Sell, to_ticks(10.01, 0.01), 4);
    EXPECT_TRUE(book.can_fill_fully(Side::Buy, to_ticks(10.01, 0.01), 8));
    EXPECT_FALSE(book.can_fill_fully(Side::Buy, to_ticks(10.0, 0.01), 8));
}

TEST(BarExchange, MarketOrderFillsAtNextBarOpen) {
    SimHarness h;
    h.bar(60, 100, 101, 99, 100.5, 10'000);
    h.bar(120, 100.7, 101.5, 100.2, 101.0, 10'000);
    h.kernel.subscribe([&h](const Event& ev) {
        if (const auto* b = get_if<BarEvent>(&ev.payload)) {
            if (ev.ts.ns == 60'000'000'000LL && b->instrument.value == 1)
                h.sim.submit(make_order(1, Side::Buy, OrderType::Market, 100));
        }
    });
    h.run(300);
    ASSERT_EQ(h.fills.size(), 1u);
    EXPECT_DOUBLE_EQ(h.fills[0].price, 100.7);
    EXPECT_DOUBLE_EQ(h.fills[0].quantity, 100.0);
    ASSERT_EQ(h.acks.size(), 1u);
    EXPECT_TRUE(h.acks[0].accepted);
}

TEST(BarExchange, ConservativeLimitNeedsStrictPenetration) {
    SimHarness h;
    h.bar(60, 100, 101, 99, 100.5, 10'000);
    h.bar(120, 100.5, 101.0, 99.5, 100.0, 10'000);
    h.bar(180, 100.0, 100.8, 99.0, 99.2, 10'000);
    h.kernel.subscribe([&h](const Event& ev) {
        if (get_if<BarEvent>(&ev.payload) != nullptr && ev.ts.ns == 60'000'000'000LL)
            h.sim.submit(make_order(1, Side::Buy, OrderType::Limit, 10, 99.5));
    });
    h.run(300);
    ASSERT_EQ(h.fills.size(), 1u);
    EXPECT_EQ(h.fills[0].fill.value, 1u);
    EXPECT_LE(h.fills[0].price, 99.5);
    EXPECT_EQ(format_iso8601_utc(TimestampNs{180'000'000'000LL}).substr(0, 4), "1970");
}

TEST(BarExchange, ParticipationCapCausesPartialFills) {
    SimHarness h;
    h.bar(60, 100, 101, 99, 100.5, 1'000);
    h.bar(120, 100.5, 101, 99.5, 100.2, 1'000);
    h.bar(180, 100.2, 101, 99.5, 100.4, 1'000);
    h.kernel.subscribe([&h](const Event& ev) {
        if (get_if<BarEvent>(&ev.payload) != nullptr && ev.ts.ns == 60'000'000'000LL)
            h.sim.submit(make_order(1, Side::Buy, OrderType::Market, 150));
    });
    h.run(300);
    ASSERT_EQ(h.fills.size(), 2u);
    EXPECT_DOUBLE_EQ(h.fills[0].quantity, 100.0);
    EXPECT_DOUBLE_EQ(h.fills[1].quantity, 50.0);
}

TEST(BarExchange, StopTriggersOnRangeCross) {
    SimHarness h;
    h.bar(60, 100, 100.5, 99.5, 100, 10'000);
    h.bar(120, 100, 102.5, 99.8, 102, 10'000);
    h.kernel.subscribe([&h](const Event& ev) {
        if (get_if<BarEvent>(&ev.payload) != nullptr && ev.ts.ns == 60'000'000'000LL)
            h.sim.submit(make_order(1, Side::Buy, OrderType::Stop, 10, 0, 102.0));
    });
    h.run(300);
    ASSERT_EQ(h.fills.size(), 1u);
    EXPECT_GE(h.fills[0].price, 102.0);
}

TEST(BarExchange, HaltBlocksAcceptanceAndFills) {
    SimHarness h;
    h.kernel.schedule(TimestampNs{30'000'000'000LL}, EventPriority::Session,
                      SessionEvent{SessionEvent::Kind::Halt, InstrumentId{1}});
    h.bar(60, 100, 101, 99, 100.5, 10'000);
    h.bar(120, 100.5, 101, 99.5, 100.2, 10'000);
    h.kernel.subscribe([&h](const Event& ev) {
        if (get_if<BarEvent>(&ev.payload) != nullptr && ev.ts.ns == 60'000'000'000LL)
            h.sim.submit(make_order(1, Side::Buy, OrderType::Market, 10));
    });
    h.run(300);
    EXPECT_EQ(h.fills.size(), 0u);
    ASSERT_EQ(h.acks.size(), 1u);
    EXPECT_FALSE(h.acks[0].accepted);
    EXPECT_EQ(h.acks[0].reason, "instrument halted");
}

TEST(BarExchange, IocCancelsUnfilledRemainder) {
    SimHarness h;
    h.bar(60, 100, 101, 99, 100.5, 1'000);
    h.bar(120, 100.5, 101, 99.5, 100.2, 1'000);
    h.kernel.subscribe([&h](const Event& ev) {
        if (get_if<BarEvent>(&ev.payload) != nullptr && ev.ts.ns == 60'000'000'000LL)
            h.sim.submit(make_order(1, Side::Buy, OrderType::Market, 500, 0, 0, TimeInForce::IOC));
    });
    h.run(300);
    ASSERT_EQ(h.fills.size(), 1u);
    EXPECT_DOUBLE_EQ(h.fills[0].quantity, 100.0);
    ASSERT_EQ(h.cancels.size(), 1u);
    EXPECT_TRUE(h.cancels[0].cancelled);
}

TEST(BarExchange, SquareRootImpactWorsensPrice) {
    Kernel kernel;
    auto master = one_equity();
    BarExchangeSim sim(kernel, master, make_unique<FixedLatency>(milliseconds(1)),
                       make_unique<SquareRootImpact>(0.5));
    vector<FillEvent> fills;
    kernel.subscribe([&](const Event& ev) {
        sim.on_event(ev);
        if (const auto* f = get_if<FillEvent>(&ev.payload)) fills.push_back(*f);
        if (get_if<BarEvent>(&ev.payload) != nullptr && ev.ts.ns == 60'000'000'000LL)
            sim.submit(make_order(1, Side::Buy, OrderType::Market, 400));
    });
    BarEvent b;
    b.instrument = InstrumentId{1};
    b.open = 100;
    b.high = 105;
    b.low = 95;
    b.close = 100;
    b.volume = 10'000;
    kernel.schedule(TimestampNs{60'000'000'000LL}, EventPriority::MarketData, b);
    BarEvent b2 = b;
    b2.open = 100;
    kernel.schedule(TimestampNs{120'000'000'000LL}, EventPriority::MarketData, b2);
    kernel.run(TimestampNs{300'000'000'000LL});
    ASSERT_EQ(fills.size(), 1u);
    EXPECT_GT(fills[0].price, 100.0);
}

TEST(Portfolio, FifoRealizedPnlAndIdentity) {
    auto master = one_equity();
    FxRates fx("USD");
    PortfolioConfig cfg;
    cfg.initial_capital = 100'000;
    Portfolio pf(cfg, master, fx);
    const TimestampNs t1{1}, t2{2}, t3{3};
    FillEvent buy1{FillId{1}, OrderId{1}, InstrumentId{1}, Side::Buy, 100, 10.0, 1.0};
    FillEvent buy2{FillId{2}, OrderId{2}, InstrumentId{1}, Side::Buy, 100, 12.0, 1.0};
    FillEvent sell{FillId{3}, OrderId{3}, InstrumentId{1}, Side::Sell, 150, 15.0, 1.5};
    pf.apply_fill(buy1, t1);
    pf.check_identity(t1);
    pf.apply_fill(buy2, t2);
    pf.check_identity(t2);
    pf.apply_fill(sell, t3);
    pf.check_identity(t3);
    EXPECT_DOUBLE_EQ(pf.position(InstrumentId{1}).quantity, 50.0);
    EXPECT_DOUBLE_EQ(pf.realized_pnl_base(), 100 * 5.0 + 50 * 3.0);
    EXPECT_DOUBLE_EQ(pf.commissions_base(), 3.5);
    pf.mark(InstrumentId{1}, 14.0);
    EXPECT_DOUBLE_EQ(pf.unrealized_pnl(t3), 50 * 2.0);
    pf.check_identity(t3);
}

TEST(Portfolio, ShortPositionsAndFlip) {
    auto master = one_equity();
    FxRates fx("USD");
    PortfolioConfig cfg;
    Portfolio pf(cfg, master, fx);
    FillEvent sell{FillId{1}, OrderId{1}, InstrumentId{1}, Side::Sell, 100, 20.0, 0};
    FillEvent buy{FillId{2}, OrderId{2}, InstrumentId{1}, Side::Buy, 160, 18.0, 0};
    pf.apply_fill(sell, TimestampNs{1});
    pf.check_identity(TimestampNs{1});
    EXPECT_DOUBLE_EQ(pf.position(InstrumentId{1}).quantity, -100.0);
    pf.apply_fill(buy, TimestampNs{2});
    pf.check_identity(TimestampNs{2});
    EXPECT_DOUBLE_EQ(pf.position(InstrumentId{1}).quantity, 60.0);
    EXPECT_DOUBLE_EQ(pf.realized_pnl_base(), 100 * 2.0);
    EXPECT_DOUBLE_EQ(pf.position(InstrumentId{1}).avg_cost, 18.0);
}

TEST(Portfolio, SplitAdjustsPositionAndIdentityHolds) {
    auto master = one_equity();
    FxRates fx("USD");
    PortfolioConfig cfg;
    Portfolio pf(cfg, master, fx);
    pf.apply_fill(FillEvent{FillId{1}, OrderId{1}, InstrumentId{1}, Side::Buy, 100, 50.0, 0},
                  TimestampNs{1});
    pf.mark(InstrumentId{1}, 50.0);
    CorporateActionEvent split;
    split.kind = CorporateActionEvent::Kind::Split;
    split.instrument = InstrumentId{1};
    split.ratio = 2.0;
    pf.apply_corporate_action(split, TimestampNs{2});
    EXPECT_DOUBLE_EQ(pf.position(InstrumentId{1}).quantity, 200.0);
    EXPECT_DOUBLE_EQ(pf.position(InstrumentId{1}).avg_cost, 25.0);
    pf.check_identity(TimestampNs{2});
}

TEST(Portfolio, DividendCreditsCash) {
    auto master = one_equity();
    FxRates fx("USD");
    PortfolioConfig cfg;
    cfg.initial_capital = 10'000;
    Portfolio pf(cfg, master, fx);
    pf.apply_fill(FillEvent{FillId{1}, OrderId{1}, InstrumentId{1}, Side::Buy, 100, 10.0, 0},
                  TimestampNs{1});
    CorporateActionEvent div;
    div.kind = CorporateActionEvent::Kind::CashDividend;
    div.instrument = InstrumentId{1};
    div.amount = 0.5;
    pf.apply_corporate_action(div, TimestampNs{2});
    EXPECT_DOUBLE_EQ(pf.dividends_base(), 50.0);
    pf.mark(InstrumentId{1}, 10.0);
    EXPECT_DOUBLE_EQ(pf.equity(TimestampNs{2}), 10'000 + 50.0);
    pf.check_identity(TimestampNs{2});
}

TEST(Portfolio, MultiCurrencyEquityConversion) {
    InstrumentMaster m;
    Instrument i;
    i.id = InstrumentId{1};
    i.symbol = "EURSTK";
    i.currency = "EUR";
    i.listing = TimestampNs{0};
    m.add(i);
    FxRates fx("USD");
    fx.set("EUR", TimestampNs{0}, 1.10);
    PortfolioConfig cfg;
    cfg.initial_capital = 100'000;
    Portfolio pf(cfg, m, fx);
    pf.apply_fill(FillEvent{FillId{1}, OrderId{1}, InstrumentId{1}, Side::Buy, 100, 50.0, 0},
                  TimestampNs{1});
    pf.mark(InstrumentId{1}, 55.0);
    pf.check_identity(TimestampNs{1});
    const double expected = 100'000 + 100 * 5.0 * 1.10;
    EXPECT_NEAR(pf.equity(TimestampNs{1}), expected, 1e-6);
    fx.set("EUR", TimestampNs{10}, 1.20);
    pf.check_identity(TimestampNs{20});
}

TEST(Portfolio, FinancingAccruesOnNegativeCash) {
    auto master = one_equity();
    FxRates fx("USD");
    PortfolioConfig cfg;
    cfg.initial_capital = 1'000;
    cfg.borrow_rate_annual = 0.05;
    Portfolio pf(cfg, master, fx);
    pf.apply_fill(FillEvent{FillId{1}, OrderId{1}, InstrumentId{1}, Side::Buy, 100, 20.0, 0},
                  TimestampNs{1});
    pf.accrue_financing(days(365));
    EXPECT_GT(pf.financing_base(), 0.0);
    pf.mark(InstrumentId{1}, 20.0);
    pf.check_identity(TimestampNs{2});
}

TEST(Portfolio, LeverageBreachDetection) {
    auto master = one_equity();
    FxRates fx("USD");
    PortfolioConfig cfg;
    cfg.initial_capital = 10'000;
    cfg.max_gross_leverage = 2.0;
    Portfolio pf(cfg, master, fx);
    pf.mark(InstrumentId{1}, 100.0);
    EXPECT_FALSE(pf.would_breach_leverage(TimestampNs{1}, 100 * 100.0));
    EXPECT_TRUE(pf.would_breach_leverage(TimestampNs{1}, 500 * 100.0));
}
