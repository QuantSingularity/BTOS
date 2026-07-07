#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "btos/data/bar.hpp"
#include "btos/data/calendar.hpp"

using namespace std;

namespace btos {

struct ValidationIssue {
    enum class Kind : std::uint8_t {
        NonMonotonicTimestamp,
        DuplicateTimestamp,
        Gap,
        OhlcInconsistent,
        NonPositivePrice,
        NegativeVolume,
        ReturnOutlier,
    } kind;
    std::size_t row{0};
    std::string detail;
};

struct ValidationOptions {
    double outlier_z{8.0};
    bool check_gaps{true};
};

std::vector<ValidationIssue> validate_bars(const std::vector<Bar>& bars, DurationNs period,
                                           const Calendar& calendar,
                                           const ValidationOptions& opts = {});

std::string format_issues(const std::vector<ValidationIssue>& issues);

}
