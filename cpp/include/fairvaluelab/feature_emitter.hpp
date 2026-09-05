#pragma once

#include "fairvaluelab/market_event.hpp"
#include "fairvaluelab/order_book.hpp"

#include <cstdint>
#include <iosfwd>
#include <map>
#include <optional>
#include <vector>

namespace fairvaluelab {

enum class SampleKind : std::uint8_t {
    Clock,
    Event,
};

struct FeatureSet {
    SampleKind sample_kind{SampleKind::Event};
    VenueId venue_id{};
    TimestampNs exchange_timestamp_ns{};
    TimestampNs local_receipt_timestamp_ns{};
    TimestampNs sample_timestamp_ns{};
    std::optional<PriceTicks> spread_ticks;
    std::optional<double> mid_price;
    std::optional<double> microprice;
    std::optional<double> imbalance_l1;
    std::optional<double> imbalance_l3;
    std::optional<double> imbalance_l5;
    Quantity bid_depth{};
    Quantity ask_depth{};
    std::optional<double> book_slope;
    TimestampNs time_since_last_update_ns{};
};

[[nodiscard]] FeatureSet compute_features(const OrderBook& book, VenueId venue_id,
                                          TimestampNs exchange_timestamp_ns,
                                          TimestampNs local_receipt_timestamp_ns,
                                          TimestampNs sample_timestamp_ns,
                                          SampleKind sample_kind);

class FeatureEmitter {
  public:
    explicit FeatureEmitter(TimestampNs clock_interval_ns);
    [[nodiscard]] std::vector<FeatureSet> process(const BookUpdate& update);

  private:
    struct VenueState {
        OrderBook book;
        bool initialized{};
        TimestampNs exchange_timestamp_ns{};
        TimestampNs local_receipt_timestamp_ns{};
        TimestampNs next_clock_timestamp_ns{};
    };

    TimestampNs clock_interval_ns_;
    std::optional<TimestampNs> current_timestamp_ns_;
    std::map<VenueId, VenueState> states_;
};

[[nodiscard]] std::uint64_t write_feature_csv(std::istream& input, std::ostream& output,
                                              TimestampNs clock_interval_ns);

}
