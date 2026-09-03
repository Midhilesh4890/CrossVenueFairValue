#pragma once

#include <cstdint>

namespace fairvaluelab {

using PriceTicks = std::int64_t;
using Quantity = std::uint64_t;
using TimestampNs = std::uint64_t;
using SequenceNumber = std::uint64_t;

enum class Side : std::uint8_t {
    Bid,
    Ask,
};

struct BookUpdate {
    Side side{};
    PriceTicks price_ticks{};
    Quantity quantity{};
    TimestampNs exchange_timestamp_ns{};
    TimestampNs receive_timestamp_ns{};
    SequenceNumber sequence_number{};
};

} // namespace fairvaluelab
