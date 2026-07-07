#include <gtest/gtest.h>
#include <rapidcheck/gtest.h>

#include <cmath>
#include <map>

#include "btos/data/instrument_master.hpp"
#include "btos/exec/order_book.hpp"
#include "btos/portfolio/fx.hpp"
#include "btos/portfolio/portfolio.hpp"

using namespace std;
using namespace btos;

namespace {

struct BookOp {
    int action;
    bool buy;
    int price_ticks;
    int qty;
    uint64_t cancel_target;
};

InstrumentMaster prop_master() {
    InstrumentMaster m;
    Instrument i;
    i.id = InstrumentId{1};
    i.symbol = "P";
    i.listing = TimestampNs{0};
    m.add(i);
    return m;
}

}

RC_GTEST_PROP(OrderBookProps, NoNegativeSizesAndBidBelowAsk, ()) {
    const auto ops = *rc::gen::container<vector<BookOp>>(
        rc::gen::build<BookOp>(
            rc::gen::set(&BookOp::action, rc::gen::inRange(0, 3)),
            rc::gen::set(&BookOp::buy, rc::gen::arbitrary<bool>()),
            rc::gen::set(&BookOp::price_ticks, rc::gen::inRange(990, 1011)),
            rc::gen::set(&BookOp::qty, rc::gen::inRange(1, 50)),
            rc::gen::set(&BookOp::cancel_target, rc::gen::inRange<uint64_t>(1, 40))));
    OrderBook book(0.01);
    uint64_t next_id = 1;
    for (const auto& op : ops) {
        const Side side = op.buy ? Side::Buy : Side::Sell;
        if (op.action == 0) {
            book.submit_limit(OrderId{next_id++}, side, PriceTicks{op.price_ticks},
                              static_cast<double>(op.qty));
        } else if (op.action == 1) {
            book.submit_market(OrderId{next_id++}, side, static_cast<double>(op.qty), nullptr);
        } else {
            book.cancel(OrderId{op.cancel_target});
        }
        RC_ASSERT(book.total_resting() >= -1e-9);
        const auto bid = book.best_bid();
        const auto ask = book.best_ask();
        if (bid && ask) RC_ASSERT(*bid < *ask);
    }
}

RC_GTEST_PROP(OrderBookProps, SizeConservationAcrossMatching, ()) {
    const auto resting_qtys = *rc::gen::container<vector<int>>(rc::gen::inRange(1, 30));
    RC_PRE(!resting_qtys.empty());
    const auto taker_qty = *rc::gen::inRange(1, 200);
    OrderBook book(0.01);
    uint64_t next_id = 1;
    double resting_total = 0;
    for (int q : resting_qtys) {
        book.submit_limit(OrderId{next_id++}, Side::Sell, PriceTicks{1000}, q);
        resting_total += q;
    }
    double unfilled = 0;
    const auto fills =
        book.submit_market(OrderId{next_id++}, Side::Buy, taker_qty, &unfilled);
    double filled = 0;
    for (const auto& f : fills) filled += f.quantity;
    RC_ASSERT(fabs(filled + unfilled - taker_qty) < 1e-9);
    RC_ASSERT(fabs(book.total_resting() - (resting_total - filled)) < 1e-9);
}

RC_GTEST_PROP(OrderBookProps, FullyCancelledBookIsEmpty, ()) {
    const auto n = *rc::gen::inRange(1, 40);
    OrderBook book(0.01);
    for (int i = 1; i <= n; ++i)
        book.submit_limit(OrderId{static_cast<uint64_t>(i)}, i % 2 == 0 ? Side::Buy : Side::Sell,
                          PriceTicks{i % 2 == 0 ? 995 : 1005}, i);
    for (int i = 1; i <= n; ++i) book.cancel(OrderId{static_cast<uint64_t>(i)});
    RC_ASSERT(book.total_resting() < 1e-9);
    RC_ASSERT(!book.best_bid().has_value());
    RC_ASSERT(!book.best_ask().has_value());
}

RC_GTEST_PROP(PortfolioProps, AccountingIdentityUnderRandomFills, ()) {
    struct FillSpec {
        bool buy;
        int qty;
        int price_cents;
        int mark_cents;
    };
    const auto specs = *rc::gen::container<vector<FillSpec>>(
        rc::gen::build<FillSpec>(rc::gen::set(&FillSpec::buy, rc::gen::arbitrary<bool>()),
                                 rc::gen::set(&FillSpec::qty, rc::gen::inRange(1, 200)),
                                 rc::gen::set(&FillSpec::price_cents, rc::gen::inRange(500, 20000)),
                                 rc::gen::set(&FillSpec::mark_cents, rc::gen::inRange(500, 20000))));
    RC_PRE(!specs.empty());
    auto master = prop_master();
    FxRates fx("USD");
    PortfolioConfig cfg;
    cfg.initial_capital = 1'000'000;
    Portfolio pf(cfg, master, fx);
    uint64_t id = 1;
    TimestampNs t{0};
    for (const auto& s : specs) {
        t = t + seconds(1);
        FillEvent f{FillId{id}, OrderId{id}, InstrumentId{1},
                    s.buy ? Side::Buy : Side::Sell, static_cast<double>(s.qty),
                    s.price_cents / 100.0, 0.01 * s.qty};
        ++id;
        pf.apply_fill(f, t);
        pf.mark(InstrumentId{1}, s.mark_cents / 100.0);
        pf.check_identity(t);
    }
    RC_ASSERT(isfinite(pf.equity(t)));
}
