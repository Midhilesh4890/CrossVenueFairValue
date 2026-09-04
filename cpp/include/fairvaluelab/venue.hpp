#pragma once

#include "fairvaluelab/market_event.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace fairvaluelab {

struct Rational {
    std::int64_t numerator{};
    std::uint64_t denominator{1};
};

struct VenueConfig {
    VenueId venue_id{};
    std::string name;
    Rational tick_size{1, 1};
    Quantity quantity_scale_factor{1};
    std::size_t max_book_depth{64};
};

[[nodiscard]] std::optional<PriceTicks> price_to_ticks(Rational price, Rational tick_size) noexcept;
[[nodiscard]] std::optional<Quantity> scale_quantity(Quantity raw_quantity,
                                                     Quantity scale_factor) noexcept;

}
