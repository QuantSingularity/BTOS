#include <benchmark/benchmark.h>

#include <random>

#include "btos/core/kernel.hpp"
#include "btos/exec/order_book.hpp"

using namespace std;
using namespace btos;

namespace {

void BM_KernelDispatch(benchmark::State& state) {
    const auto n = static_cast<size_t>(state.range(0));
    for (auto _ : state) {
        state.PauseTiming();
        Kernel k;
        size_t seen = 0;
        k.subscribe([&seen](const Event&) { ++seen; });
        BarEvent b;
        b.instrument = InstrumentId{1};
        for (size_t i = 0; i < n; ++i)
            k.schedule(TimestampNs{static_cast<int64_t>(i + 1)}, EventPriority::MarketData, b);
        state.ResumeTiming();
        k.run(TimestampNs{static_cast<int64_t>(n + 1)});
        benchmark::DoNotOptimize(seen);
    }
    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()) * static_cast<int64_t>(n));
    state.counters["events_per_sec"] =
        benchmark::Counter(static_cast<double>(state.iterations()) * static_cast<double>(n),
                           benchmark::Counter::kIsRate);
}

void BM_KernelScheduleAndDispatchInterleaved(benchmark::State& state) {
    const auto n = static_cast<size_t>(state.range(0));
    for (auto _ : state) {
        Kernel k;
        size_t emitted = 0;
        k.subscribe([&](const Event& ev) {
            if (emitted < n && holds_alternative<TimerEvent>(ev.payload)) {
                ++emitted;
                k.schedule(ev.ts + seconds(1), EventPriority::Timer, TimerEvent{emitted});
            }
        });
        k.schedule(TimestampNs{1}, EventPriority::Timer, TimerEvent{0});
        k.run(TimestampNs{static_cast<int64_t>((n + 2) * 1'000'000'000LL)});
        benchmark::DoNotOptimize(emitted);
    }
    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()) * static_cast<int64_t>(n));
    state.counters["events_per_sec"] =
        benchmark::Counter(static_cast<double>(state.iterations()) * static_cast<double>(n),
                           benchmark::Counter::kIsRate);
}

void BM_OrderBookMatching(benchmark::State& state) {
    const auto n = static_cast<size_t>(state.range(0));
    mt19937_64 rng(42);
    uniform_int_distribution<int> px(990, 1010);
    uniform_int_distribution<int> qty(1, 20);
    uniform_int_distribution<int> side(0, 1);
    size_t fills_total = 0;
    for (auto _ : state) {
        state.PauseTiming();
        OrderBook book(0.01);
        uint64_t id = 1;
        for (size_t i = 0; i < 512; ++i)
            book.submit_limit(OrderId{id++}, i % 2 == 0 ? Side::Buy : Side::Sell,
                              PriceTicks{i % 2 == 0 ? 995 : 1005}, 10);
        state.ResumeTiming();
        for (size_t i = 0; i < n; ++i) {
            const auto fills = book.submit_limit(
                OrderId{id++}, side(rng) == 0 ? Side::Buy : Side::Sell, PriceTicks{px(rng)},
                static_cast<double>(qty(rng)));
            fills_total += fills.size();
        }
        benchmark::DoNotOptimize(fills_total);
    }
    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()) * static_cast<int64_t>(n));
    state.counters["orders_per_sec"] =
        benchmark::Counter(static_cast<double>(state.iterations()) * static_cast<double>(n),
                           benchmark::Counter::kIsRate);
    state.counters["fills_per_sec"] =
        benchmark::Counter(static_cast<double>(fills_total), benchmark::Counter::kIsRate);
}

BENCHMARK(BM_KernelDispatch)->Arg(100000);
BENCHMARK(BM_KernelScheduleAndDispatchInterleaved)->Arg(100000);
BENCHMARK(BM_OrderBookMatching)->Arg(50000);

}

BENCHMARK_MAIN();
