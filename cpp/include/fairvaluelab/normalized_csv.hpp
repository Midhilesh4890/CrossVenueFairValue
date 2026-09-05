#pragma once

#include "fairvaluelab/market_event.hpp"

#include <optional>
#include <string_view>

namespace fairvaluelab {

inline constexpr std::string_view normalized_csv_header =
    "sequence_number,venue_id,exchange_timestamp_ns,local_receipt_timestamp_ns,side,"
    "price_ticks,quantity";

[[nodiscard]] bool is_normalized_csv_header(std::string_view line) noexcept;
[[nodiscard]] BookUpdate parse_normalized_event(std::string_view line);
[[nodiscard]] std::optional<VenueId> find_normalized_venue_id(std::string_view line) noexcept;

}
