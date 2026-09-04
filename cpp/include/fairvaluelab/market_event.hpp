#pragma once

#include <cstdint>

namespace fairvaluelab {

using PriceTicks = std::int64_t;
using Quantity = std::uint64_t;
using TimestampNs = std::uint64_t;
using SequenceNumber = std::uint64_t;
using VenueId = std::uint32_t;

enum class Side : std::uint8_t {
    Bid,
    Ask,
};

struct BookUpdate {
    VenueId venue_id{};
    Side side{};
    PriceTicks price_ticks{};
    Quantity quantity{};
    TimestampNs exchange_timestamp_ns{};
    TimestampNs local_receipt_timestamp_ns{exchange_timestamp_ns};
    SequenceNumber sequence_number{};
};

}
