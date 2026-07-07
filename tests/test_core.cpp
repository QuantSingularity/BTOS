#include <gtest/gtest.h>

#include <filesystem>

#include "btos/core/event_log.hpp"
#include "btos/core/kernel.hpp"
#include "btos/core/rng.hpp"
#include "btos/core/thread_pool.hpp"

using namespace std;
using namespace btos;

TEST(Time, ParseFormatRoundTrip) {
    const string s = "2021-03-05T14:30:15.250000000Z";
    const TimestampNs t = parse_iso8601_utc(s);
    EXPECT_EQ(format_iso8601_utc(t), s);
}

TEST(Time, DateOnlyParsesToMidnight) {
    const TimestampNs t = parse_iso8601_utc("2020-01-01");
    EXPECT_EQ(ns_since_midnight(t), 0);
    EXPECT_EQ(format_iso8601_utc(t).substr(0, 10), "2020-01-01");
}

TEST(Time, KnownEpochValues) {
    EXPECT_EQ(parse_iso8601_utc("1970-01-01T00:00:00Z").ns, 0);
    EXPECT_EQ(parse_iso8601_utc("1970-01-02T00:00:00Z").ns, 86'400'000'000'000LL);
    EXPECT_EQ(parse_iso8601_utc("2000-02-29T00:00:00Z").ns / 86'400'000'000'000LL, 11016);
}

TEST(Time, RejectsMalformed) {
    EXPECT_THROW(parse_iso8601_utc("2020-13-01"), invalid_argument);
    EXPECT_THROW(parse_iso8601_utc("2020-02-30"), invalid_argument);
    EXPECT_THROW(parse_iso8601_utc("2020-01-01T25:00:00Z"), invalid_argument);
    EXPECT_THROW(parse_iso8601_utc("garbage"), invalid_argument);
}

TEST(Time, Arithmetic) {
    const TimestampNs t = parse_iso8601_utc("2020-06-01T12:00:00Z");
    EXPECT_EQ((t + hours(2)).ns - t.ns, 7'200'000'000'000LL);
    EXPECT_LT(t, t + seconds(1));
}

TEST(Rng, StreamsAreDeterministicAndDistinct) {
    RngProvider a(42), b(42), c(7);
    auto s1 = a.stream("x");
    auto s2 = b.stream("x");
    auto s3 = a.stream("y");
    auto s4 = c.stream("x");
    EXPECT_EQ(s1(), s2());
    EXPECT_NE(s1(), s3());
    EXPECT_NE(s2(), s4());
}

TEST(Hash, Fnv1a64KnownValue) {
    EXPECT_EQ(fnv1a64("hello"), 0xa430d84680aabd0bULL);
    EXPECT_EQ(to_hex(0xdeadbeefULL), "00000000deadbeef");
}

TEST(Kernel, InvariantEventOrdering) {
    Kernel k;
    vector<string> order;
    k.subscribe([&](const Event& ev) {
        if (holds_alternative<BarEvent>(ev.payload)) order.push_back("bar");
        else if (holds_alternative<OrderAckEvent>(ev.payload)) order.push_back("ack");
        else if (holds_alternative<FillEvent>(ev.payload)) order.push_back("fill");
        else if (holds_alternative<TimerEvent>(ev.payload)) order.push_back("timer");
    });
    const TimestampNs t{1'000};
    FillEvent f;
    f.fill = FillId{1};
    k.schedule(t, EventPriority::Fill, f);
    k.schedule(t, EventPriority::Timer, TimerEvent{7});
    k.schedule(t, EventPriority::OrderAck, OrderAckEvent{OrderId{1}, true, ""});
    BarEvent b;
    b.instrument = InstrumentId{1};
    k.schedule(t, EventPriority::MarketData, b);
    k.run(TimestampNs{2'000});
    ASSERT_EQ(order.size(), 4u);
    EXPECT_EQ(order[0], "bar");
    EXPECT_EQ(order[1], "ack");
    EXPECT_EQ(order[2], "fill");
    EXPECT_EQ(order[3], "timer");
}

TEST(Kernel, EqualPriorityBreaksBySequence) {
    Kernel k;
    vector<uint64_t> tokens;
    k.subscribe([&](const Event& ev) {
        if (const auto* t = get_if<TimerEvent>(&ev.payload)) tokens.push_back(t->token);
    });
    const TimestampNs t{500};
    k.schedule(t, EventPriority::Timer, TimerEvent{1});
    k.schedule(t, EventPriority::Timer, TimerEvent{2});
    k.schedule(t, EventPriority::Timer, TimerEvent{3});
    k.run(TimestampNs{1'000});
    EXPECT_EQ(tokens, (vector<uint64_t>{1, 2, 3}));
}

TEST(Kernel, SchedulingInThePastThrows) {
    Kernel k;
    k.subscribe([](const Event&) {});
    k.schedule(TimestampNs{100}, EventPriority::Timer, TimerEvent{1});
    k.run(TimestampNs{200});
    EXPECT_THROW(k.schedule(TimestampNs{50}, EventPriority::Timer, TimerEvent{2}), logic_error);
}

TEST(Kernel, ClockAdvancesWithDispatch) {
    Kernel k;
    TimestampNs seen{0};
    k.subscribe([&](const Event& ev) { seen = ev.ts; });
    k.schedule(TimestampNs{123}, EventPriority::Timer, TimerEvent{1});
    k.run(TimestampNs{1'000});
    EXPECT_EQ(seen.ns, 123);
    EXPECT_EQ(k.now().ns, 123);
}

TEST(EventLog, SerializeDeserializeRoundTrip) {
    Event ev;
    ev.ts = TimestampNs{42};
    ev.priority = EventPriority::Fill;
    ev.seq = 9;
    FillEvent f;
    f.fill = FillId{3};
    f.order = OrderId{5};
    f.instrument = InstrumentId{2};
    f.side = Side::Sell;
    f.quantity = 10.5;
    f.price = 99.25;
    f.commission = 0.35;
    ev.payload = f;
    vector<byte> buf;
    serialize_event(ev, buf);
    size_t off = 0;
    auto back = deserialize_event(buf, off);
    ASSERT_TRUE(back.has_value());
    EXPECT_EQ(back->ts.ns, 42);
    EXPECT_EQ(back->seq, 9u);
    const auto& g = get<FillEvent>(back->payload);
    EXPECT_EQ(g.order.value, 5u);
    EXPECT_DOUBLE_EQ(g.price, 99.25);
    EXPECT_EQ(off, buf.size());
}

TEST(EventLog, WriterReaderDigestsMatch) {
    const auto path = (filesystem::temp_directory_path() / "btos_test_log.bin").string();
    {
        EventLogWriter w(path);
        Kernel k;
        k.set_sink(&w);
        k.subscribe([](const Event&) {});
        BarEvent b;
        b.instrument = InstrumentId{1};
        b.close = 101.0;
        k.schedule(TimestampNs{10}, EventPriority::MarketData, b);
        k.schedule(TimestampNs{20}, EventPriority::Timer, TimerEvent{1});
        k.run(TimestampNs{100});
        w.close();
        EventLogReader r(path);
        EXPECT_EQ(r.digest(), w.digest());
        EXPECT_EQ(r.events().size(), 2u);
    }
    filesystem::remove(path);
}

TEST(ThreadPool, ExecutesAllTasks) {
    ThreadPool pool(2, 8);
    atomic<int> counter{0};
    for (int i = 0; i < 64; ++i) pool.submit([&counter] { ++counter; });
    pool.wait_idle();
    EXPECT_EQ(counter.load(), 64);
}

TEST(ThreadPool, BackpressureBlocksAndDrains) {
    ThreadPool pool(1, 2);
    atomic<int> done{0};
    for (int i = 0; i < 16; ++i)
        pool.submit([&done] {
            this_thread::sleep_for(chrono::milliseconds(1));
            ++done;
        });
    pool.wait_idle();
    EXPECT_EQ(done.load(), 16);
}
