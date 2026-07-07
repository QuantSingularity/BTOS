#pragma once

#include <cstdint>

#include "btos/core/ids.hpp"
#include "btos/core/time.hpp"

using namespace std;

namespace btos {

struct Bar {
    TimestampNs ts;
    double open{0}, high{0}, low{0}, close{0};
    double volume{0};
};

struct Trade {
    TimestampNs ts;
    double price{0};
    double size{0};
    Side aggressor{Side::Buy};
};

}
