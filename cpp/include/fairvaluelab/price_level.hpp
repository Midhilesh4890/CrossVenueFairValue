#pragma once

#include "fairvaluelab/market_event.hpp"

namespace fairvaluelab {

struct PriceLevel {
    PriceTicks price_ticks{};
    Quantity quantity{};

    bool operator==(const PriceLevel&) const = default;
};

} // namespace fairvaluelab
