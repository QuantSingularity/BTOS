#pragma once

#include <cstdint>
#include <fstream>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "btos/core/hash.hpp"
#include "btos/core/kernel.hpp"

using namespace std;

namespace btos {

void serialize_event(const Event& ev, std::vector<std::byte>& out);

std::optional<Event> deserialize_event(const std::vector<std::byte>& buf, std::size_t& offset);

class EventLogWriter final : public IEventSink {
  public:

    explicit EventLogWriter(const std::string& path);
    ~EventLogWriter() override;
    EventLogWriter(const EventLogWriter&) = delete;
    EventLogWriter& operator=(const EventLogWriter&) = delete;

    void on_event(const Event& ev) override;

    void close();

    [[nodiscard]] std::uint64_t digest() const { return hash_.digest(); }

    [[nodiscard]] std::uint64_t count() const { return count_; }

  private:
    std::ofstream out_;
    Fnv1a64 hash_;
    std::vector<std::byte> scratch_;
    std::uint64_t count_{0};
};

class EventLogReader {
  public:

    explicit EventLogReader(const std::string& path);

    [[nodiscard]] const std::vector<Event>& events() const { return events_; }

    [[nodiscard]] std::uint64_t digest() const { return digest_; }

    void replay_into(Kernel& kernel) const {
        for (const auto& ev : events_) kernel.dispatch_external(ev);
    }

  private:
    std::vector<Event> events_;
    std::uint64_t digest_{0};
};

}
