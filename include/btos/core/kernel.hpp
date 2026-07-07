#pragma once

#include <cstdint>
#include <functional>
#include <queue>
#include <stdexcept>
#include <vector>

#include "btos/core/events.hpp"

using namespace std;

namespace btos {

class IEventSink;

class Kernel {
  public:
    using Handler = std::function<void(const Event&)>;

    std::size_t subscribe(Handler h) {
        subscribers_.push_back(std::move(h));
        return subscribers_.size() - 1;
    }

    void set_sink(IEventSink* sink) { sink_ = sink; }

    void schedule(TimestampNs ts, EventPriority prio, EventPayload payload) {
        if (started_ && ts < now_)
            throw std::logic_error("kernel: cannot schedule an event in the past");
        queue_.push(Event{ts, prio, next_seq_++, std::move(payload)});
    }

    void schedule(TimestampNs ts, EventPayload payload) {
        EventPriority p = default_priority(payload);
        schedule(ts, p, std::move(payload));
    }

    [[nodiscard]] TimestampNs now() const { return now_; }

    [[nodiscard]] std::uint64_t dispatched() const { return dispatched_; }

    [[nodiscard]] bool empty() const { return queue_.empty(); }

    std::uint64_t run(TimestampNs until = kMaxTimestamp) {
        started_ = true;
        std::uint64_t n = 0;
        while (!queue_.empty()) {
            const Event& top = queue_.top();
            if (top.ts > until) break;
            Event ev = top;
            queue_.pop();
            now_ = ev.ts;
            dispatch(ev);
            ++n;
        }
        return n;
    }

    void dispatch_external(const Event& ev) {
        if (started_ && ev.ts < now_)
            throw std::logic_error("kernel: replay stream is not time-ordered");
        started_ = true;
        now_ = ev.ts;
        dispatch(ev);
    }

  private:
    void dispatch(const Event& ev);

    struct Cmp {
        bool operator()(const Event& a, const Event& b) const { return EventOrder{}(b, a); }
    };

    std::priority_queue<Event, std::vector<Event>, Cmp> queue_;
    std::vector<Handler> subscribers_;
    IEventSink* sink_{nullptr};
    TimestampNs now_{kNoTimestamp};
    std::uint64_t next_seq_{0};
    std::uint64_t dispatched_{0};
    bool started_{false};
};

class IEventSink {
  public:
    virtual ~IEventSink() = default;

    virtual void on_event(const Event& ev) = 0;
};

inline void Kernel::dispatch(const Event& ev) {
    ++dispatched_;
    if (sink_ != nullptr) sink_->on_event(ev);
    for (auto& s : subscribers_) s(ev);
}

}
