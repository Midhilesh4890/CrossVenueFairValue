#pragma once

#include "fairvaluelab/market_event.hpp"
#include "fairvaluelab/order_book.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iosfwd>
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
    std::optional<double> multi_level_ofi_event_window{};
    std::optional<double> multi_level_ofi_time_window{};
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
    std::uint64_t band_ticks{5};
    std::size_t venue_capacity{64};
};

[[nodiscard]] FeatureSet compute_features(const OrderBook& book, VenueId venue_id,
                                          TimestampNs exchange_timestamp_ns,
                                          TimestampNs local_receipt_timestamp_ns,
                                          TimestampNs sample_timestamp_ns,
                                          SampleKind sample_kind);

struct FeatureEmitterDroppedEntries {
    VenueId venue_id{};
    std::uint64_t order_flow{};
    std::uint64_t trade_flow{};
};

class FeatureEmitter {
  public:
    static constexpr std::size_t maximum_event_window = 4096;

    explicit FeatureEmitter(TimestampNs clock_interval_ns);
    explicit FeatureEmitter(FeatureEmitterConfig config);
    FeatureEmitter(const FeatureEmitter& other);
    FeatureEmitter& operator=(const FeatureEmitter& other);
    FeatureEmitter(FeatureEmitter&& other) noexcept = default;
    FeatureEmitter& operator=(FeatureEmitter&& other) noexcept = default;
    [[nodiscard]] std::vector<FeatureSet> process(const BookUpdate& update);
    [[nodiscard]] std::vector<FeatureSet> process(const Trade& trade);
    [[nodiscard]] ApplyResult process(const BookUpdate& update, std::vector<FeatureSet>& output);
    void process(const Trade& trade, std::vector<FeatureSet>& output);
    [[nodiscard]] std::optional<FeatureEmitterDroppedEntries> dropped_entries(VenueId venue_id) const;
    [[nodiscard]] std::vector<FeatureEmitterDroppedEntries> dropped_entries() const;

  private:
    template <typename Value>
    class FlowBuffer {
      public:
        explicit FlowBuffer(const std::size_t capacity) : capacity_(capacity) {}

        void push_back(const Value& value) noexcept {
            if (size_ == capacity_) {
                values_[begin_] = value;
                begin_ = (begin_ + 1) % capacity_;
                ++dropped_entries_;
            } else {
                values_[(begin_ + size_) % capacity_] = value;
                ++size_;
            }
        }

        [[nodiscard]] const Value& operator[](const std::size_t index) const noexcept {
            return values_[(begin_ + index) % capacity_];
        }

        [[nodiscard]] std::size_t size() const noexcept { return size_; }
        [[nodiscard]] std::uint64_t dropped_entries() const noexcept { return dropped_entries_; }

      private:
        std::array<Value, maximum_event_window> values_{};
        std::size_t capacity_{};
        std::size_t begin_{};
        std::size_t size_{};
        std::uint64_t dropped_entries_{};
    };

    struct VenueState {
        struct OrderFlow {
            TimestampNs timestamp_ns{};
            double value{};
            std::optional<double> multi_level_value;
        };

        struct TradeFlow {
            TimestampNs timestamp_ns{};
            double signed_volume{};
            double volume{};
            std::optional<double> price_deviation;
        };

        VenueState(const VenueId id, const std::size_t capacity)
            : venue_id(id), order_flow(capacity), trade_flow(capacity) {}

        VenueId venue_id{};
        OrderBook book;
        std::array<PriceLevel, BookSide::maximum_depth> previous_bids{};
        std::array<PriceLevel, BookSide::maximum_depth> previous_asks{};
        std::size_t previous_bid_count{};
        std::size_t previous_ask_count{};
        bool initialized{};
        TimestampNs exchange_timestamp_ns{};
        TimestampNs local_receipt_timestamp_ns{};
        TimestampNs next_clock_timestamp_ns{};
        FlowBuffer<OrderFlow> order_flow;
        FlowBuffer<TradeFlow> trade_flow;
    };

    void advance(TimestampNs timestamp_ns, std::vector<FeatureSet>& output);
    [[nodiscard]] VenueState& venue_state(VenueId venue_id);
    [[nodiscard]] FeatureSet features(const VenueState& state, VenueId venue_id,
                                      TimestampNs exchange_timestamp_ns,
                                      TimestampNs local_receipt_timestamp_ns,
                                      TimestampNs sample_timestamp_ns,
                                      SampleKind sample_kind) const;

    FeatureEmitterConfig config_;
    std::optional<TimestampNs> current_timestamp_ns_;
    std::vector<VenueState> states_;
};

[[nodiscard]] std::uint64_t write_feature_csv(std::istream& input, std::ostream& output,
                                              TimestampNs clock_interval_ns);
[[nodiscard]] std::uint64_t write_feature_csv(std::istream& input, std::ostream& output,
                                              FeatureEmitterConfig config);

[[nodiscard]] std::uint64_t write_feature_csv(std::istream& input, std::ostream& output,
                                              FeatureEmitter& emitter);

}
