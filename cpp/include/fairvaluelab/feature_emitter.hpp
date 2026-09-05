#pragma once

#include "fairvaluelab/market_event.hpp"
#include "fairvaluelab/order_book.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
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
    double ofi_event_window{};
    double ofi_time_window{};
    double multi_level_ofi_event_window{};
    double multi_level_ofi_time_window{};
    double signed_trade_volume_event_window{};
    double signed_trade_volume_time_window{};
    std::uint64_t trade_count_event_window{};
    std::uint64_t trade_count_time_window{};
    std::optional<double> trade_vwap_deviation_event_window;
    std::optional<double> trade_vwap_deviation_time_window;
};

struct FeatureEmitterConfig {
    TimestampNs clock_interval_ns{100'000'000};
    std::size_t event_window{100};
    TimestampNs time_window_ns{1'000'000'000};
    std::size_t multi_level_depth{5};
};

[[nodiscard]] FeatureSet compute_features(const OrderBook& book, VenueId venue_id,
                                          TimestampNs exchange_timestamp_ns,
                                          TimestampNs local_receipt_timestamp_ns,
                                          TimestampNs sample_timestamp_ns,
                                          SampleKind sample_kind);

class FeatureEmitter {
  public:
    explicit FeatureEmitter(TimestampNs clock_interval_ns);
    explicit FeatureEmitter(FeatureEmitterConfig config);
    [[nodiscard]] std::vector<FeatureSet> process(const BookUpdate& update);
    [[nodiscard]] std::vector<FeatureSet> process(const Trade& trade);

  private:
    struct VenueState {
        struct OrderFlow {
            TimestampNs timestamp_ns{};
            double value{};
            double multi_level_value{};
        };

        struct TradeFlow {
            TimestampNs timestamp_ns{};
            double signed_volume{};
            double volume{};
            std::optional<double> price_deviation;
        };

        OrderBook book;
        std::array<PriceLevel, BookSide::maximum_depth> previous_bids{};
        std::array<PriceLevel, BookSide::maximum_depth> previous_asks{};
        std::size_t previous_bid_count{};
        std::size_t previous_ask_count{};
        bool initialized{};
        TimestampNs exchange_timestamp_ns{};
        TimestampNs local_receipt_timestamp_ns{};
        TimestampNs next_clock_timestamp_ns{};
        std::deque<OrderFlow> order_flow;
        std::deque<TradeFlow> trade_flow;
    };

    [[nodiscard]] std::vector<FeatureSet> advance(TimestampNs timestamp_ns);
    [[nodiscard]] FeatureSet features(const VenueState& state, VenueId venue_id,
                                      TimestampNs exchange_timestamp_ns,
                                      TimestampNs local_receipt_timestamp_ns,
                                      TimestampNs sample_timestamp_ns,
                                      SampleKind sample_kind) const;
    void trim(VenueState& state, TimestampNs timestamp_ns) const;

    FeatureEmitterConfig config_;
    std::optional<TimestampNs> current_timestamp_ns_;
    std::map<VenueId, VenueState> states_;
};

[[nodiscard]] std::uint64_t write_feature_csv(std::istream& input, std::ostream& output,
                                              TimestampNs clock_interval_ns);
[[nodiscard]] std::uint64_t write_feature_csv(std::istream& input, std::ostream& output,
                                              FeatureEmitterConfig config);

}
