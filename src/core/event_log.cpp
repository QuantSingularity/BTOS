#include "btos/core/event_log.hpp"

#include <cstdio>
#include <cstring>
#include <stdexcept>

using namespace std;

namespace btos {

std::string to_hex(std::uint64_t v) {
    char buf[17];
    std::snprintf(buf, sizeof(buf), "%016llx", static_cast<unsigned long long>(v));
    return buf;
}

namespace {

template <typename T>
void put(std::vector<std::byte>& out, const T& v) {
    static_assert(std::is_trivially_copyable_v<T>);
    const auto* p = reinterpret_cast<const std::byte*>(&v);
    out.insert(out.end(), p, p + sizeof(T));
}

void put_str(std::vector<std::byte>& out, const std::string& s) {
    put(out, static_cast<std::uint32_t>(s.size()));
    const auto* p = reinterpret_cast<const std::byte*>(s.data());
    out.insert(out.end(), p, p + s.size());
}

template <typename T>
T get(const std::vector<std::byte>& buf, std::size_t& off) {
    if (off + sizeof(T) > buf.size()) throw std::runtime_error("event log: truncated record");
    T v;
    std::memcpy(&v, buf.data() + off, sizeof(T));
    off += sizeof(T);
    return v;
}

std::string get_str(const std::vector<std::byte>& buf, std::size_t& off) {
    const auto n = get<std::uint32_t>(buf, off);
    if (off + n > buf.size()) throw std::runtime_error("event log: truncated string");
    std::string s(reinterpret_cast<const char*>(buf.data() + off), n);
    off += n;
    return s;
}

}

void serialize_event(const Event& ev, std::vector<std::byte>& out) {
    put(out, ev.ts.ns);
    put(out, static_cast<std::uint8_t>(ev.priority));
    put(out, ev.seq);
    put(out, static_cast<std::uint8_t>(ev.payload.index()));
    std::visit(
        [&out](const auto& p) {
            using T = std::decay_t<decltype(p)>;
            if constexpr (std::is_same_v<T, BarEvent>) {
                put(out, p.instrument.value);
                put(out, p.open); put(out, p.high); put(out, p.low); put(out, p.close);
                put(out, p.volume); put(out, p.period.ns);
            } else if constexpr (std::is_same_v<T, TickEvent>) {
                put(out, p.instrument.value);
                put(out, p.price); put(out, p.size);
                put(out, static_cast<std::uint8_t>(p.aggressor));
            } else if constexpr (std::is_same_v<T, QuoteEvent>) {
                put(out, p.instrument.value);
                put(out, p.bid); put(out, p.ask); put(out, p.bid_size); put(out, p.ask_size);
            } else if constexpr (std::is_same_v<T, OrderArrivalEvent>) {
                put(out, p.order.value);
            } else if constexpr (std::is_same_v<T, OrderAckEvent>) {
                put(out, p.order.value);
                put(out, static_cast<std::uint8_t>(p.accepted));
                put_str(out, p.reason);
            } else if constexpr (std::is_same_v<T, CancelAckEvent>) {
                put(out, p.order.value);
                put(out, static_cast<std::uint8_t>(p.cancelled));
            } else if constexpr (std::is_same_v<T, FillEvent>) {
                put(out, p.fill.value); put(out, p.order.value); put(out, p.instrument.value);
                put(out, static_cast<std::uint8_t>(p.side));
                put(out, p.quantity); put(out, p.price); put(out, p.commission);
            } else if constexpr (std::is_same_v<T, TimerEvent>) {
                put(out, p.token);
            } else if constexpr (std::is_same_v<T, SessionEvent>) {
                put(out, static_cast<std::uint8_t>(p.kind));
                put(out, p.instrument.value);
            } else if constexpr (std::is_same_v<T, CorporateActionEvent>) {
                put(out, static_cast<std::uint8_t>(p.kind));
                put(out, p.instrument.value);
                put(out, p.ratio); put(out, p.amount);
            }
        },
        ev.payload);
}

std::optional<Event> deserialize_event(const std::vector<std::byte>& buf, std::size_t& off) {
    if (off >= buf.size()) return std::nullopt;
    Event ev;
    ev.ts.ns = get<std::int64_t>(buf, off);
    ev.priority = static_cast<EventPriority>(get<std::uint8_t>(buf, off));
    ev.seq = get<std::uint64_t>(buf, off);
    const auto idx = get<std::uint8_t>(buf, off);
    switch (idx) {
        case 0: {
            BarEvent p;
            p.instrument.value = get<std::uint32_t>(buf, off);
            p.open = get<double>(buf, off); p.high = get<double>(buf, off);
            p.low = get<double>(buf, off); p.close = get<double>(buf, off);
            p.volume = get<double>(buf, off); p.period.ns = get<std::int64_t>(buf, off);
            ev.payload = p; break;
        }
        case 1: {
            TickEvent p;
            p.instrument.value = get<std::uint32_t>(buf, off);
            p.price = get<double>(buf, off); p.size = get<double>(buf, off);
            p.aggressor = static_cast<Side>(get<std::uint8_t>(buf, off));
            ev.payload = p; break;
        }
        case 2: {
            QuoteEvent p;
            p.instrument.value = get<std::uint32_t>(buf, off);
            p.bid = get<double>(buf, off); p.ask = get<double>(buf, off);
            p.bid_size = get<double>(buf, off); p.ask_size = get<double>(buf, off);
            ev.payload = p; break;
        }
        case 3: {
            OrderArrivalEvent p;
            p.order.value = get<std::uint64_t>(buf, off);
            ev.payload = p; break;
        }
        case 4: {
            OrderAckEvent p;
            p.order.value = get<std::uint64_t>(buf, off);
            p.accepted = get<std::uint8_t>(buf, off) != 0;
            p.reason = get_str(buf, off);
            ev.payload = p; break;
        }
        case 5: {
            CancelAckEvent p;
            p.order.value = get<std::uint64_t>(buf, off);
            p.cancelled = get<std::uint8_t>(buf, off) != 0;
            ev.payload = p; break;
        }
        case 6: {
            FillEvent p;
            p.fill.value = get<std::uint64_t>(buf, off);
            p.order.value = get<std::uint64_t>(buf, off);
            p.instrument.value = get<std::uint32_t>(buf, off);
            p.side = static_cast<Side>(get<std::uint8_t>(buf, off));
            p.quantity = get<double>(buf, off); p.price = get<double>(buf, off);
            p.commission = get<double>(buf, off);
            ev.payload = p; break;
        }
        case 7: {
            TimerEvent p;
            p.token = get<std::uint64_t>(buf, off);
            ev.payload = p; break;
        }
        case 8: {
            SessionEvent p;
            p.kind = static_cast<SessionEvent::Kind>(get<std::uint8_t>(buf, off));
            p.instrument.value = get<std::uint32_t>(buf, off);
            ev.payload = p; break;
        }
        case 9: {
            CorporateActionEvent p;
            p.kind = static_cast<CorporateActionEvent::Kind>(get<std::uint8_t>(buf, off));
            p.instrument.value = get<std::uint32_t>(buf, off);
            p.ratio = get<double>(buf, off); p.amount = get<double>(buf, off);
            ev.payload = p; break;
        }
        default:
            throw std::runtime_error("event log: unknown payload tag");
    }
    return ev;
}

EventLogWriter::EventLogWriter(const std::string& path) : out_(path, std::ios::binary | std::ios::trunc) {
    if (!out_) throw std::runtime_error("event log: cannot open for writing: " + path);
}

EventLogWriter::~EventLogWriter() { close(); }

void EventLogWriter::on_event(const Event& ev) {
    scratch_.clear();
    serialize_event(ev, scratch_);
    hash_.update(scratch_.data(), scratch_.size());
    out_.write(reinterpret_cast<const char*>(scratch_.data()),
               static_cast<std::streamsize>(scratch_.size()));
    ++count_;
}

void EventLogWriter::close() {
    if (out_.is_open()) {
        out_.flush();
        out_.close();
    }
}

EventLogReader::EventLogReader(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("event log: cannot open for reading: " + path);
    std::vector<std::byte> buf;
    in.seekg(0, std::ios::end);
    buf.resize(static_cast<std::size_t>(in.tellg()));
    in.seekg(0);
    in.read(reinterpret_cast<char*>(buf.data()), static_cast<std::streamsize>(buf.size()));
    Fnv1a64 h;
    h.update(buf.data(), buf.size());
    digest_ = h.digest();
    std::size_t off = 0;
    while (auto ev = deserialize_event(buf, off)) events_.push_back(std::move(*ev));
}

}
