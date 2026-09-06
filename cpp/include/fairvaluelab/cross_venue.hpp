#pragma once

#include "fairvaluelab/feature_emitter.hpp"

#include <cstddef>
#include <functional>
#include <optional>
#include <span>
#include <vector>

namespace fairvaluelab {

enum class SynchronizerUpdateStatus : std::uint8_t {
    Accepted,
    UnknownVenue,
    OutOfOrder,
    InvalidTimestamp,
};

struct CrossVenueVenueState {
    VenueId venue_id{};
    bool observed{};
    TimestampNs latest_local_receipt_timestamp_ns{};
    TimestampNs latest_exchange_timestamp_ns{};
    std::optional<PriceTicks> best_bid_ticks;
    std::optional<PriceTicks> best_ask_ticks;
    FeatureSet features;
};

struct CrossVenueSynchronizerConfig {
    TimestampNs max_staleness_ns{100'000'000};
};

struct VenueFreshness {
    bool observed{};
    bool usable{};
    std::optional<TimestampNs> age_ns;
};

struct ConsolidatedReference {
    TimestampNs sample_timestamp_ns{};
    std::optional<double> mid;
    std::optional<double> microprice;
    std::size_t valid_venue_count{};
    std::size_t mid_venue_count{};
    std::size_t microprice_venue_count{};
};

struct VenueCrossFeatures {
    VenueId venue_id{};
    bool observed{};
    bool fresh{};
    std::optional<TimestampNs> age_ns;
    std::optional<double> mid_minus_consolidated_mid;
    std::optional<double> microprice_minus_consolidated_microprice;
    std::optional<PriceTicks> spread_ticks;
    std::optional<double> imbalance_l1;
    std::optional<double> imbalance_l3;
    std::optional<double> imbalance_l5;
    std::optional<Quantity> bid_depth;
    std::optional<Quantity> ask_depth;
    std::optional<double> ofi_event_window;
    std::optional<double> ofi_time_window;
    std::optional<double> multi_level_ofi_event_window;
    std::optional<double> multi_level_ofi_time_window;
    std::optional<double> signed_trade_volume_event_window;
    std::optional<double> signed_trade_volume_time_window;
};

struct VenuePair {
    VenueId first{};
    VenueId second{};
};

struct PairwiseCrossFeatures {
    VenuePair venues;
    bool both_fresh{};
    std::optional<double> mid_difference;
    std::optional<double> microprice_difference;
    std::optional<double> imbalance_l1_difference;
    std::optional<double> imbalance_l3_difference;
    std::optional<double> imbalance_l5_difference;
    std::optional<double> ofi_event_window_difference;
    std::optional<double> ofi_time_window_difference;
    std::optional<double> signed_trade_volume_event_window_difference;
    std::optional<double> signed_trade_volume_time_window_difference;
    std::optional<std::int64_t> receipt_timestamp_difference_ns;
};

class CrossVenueSynchronizer {
  public:
    explicit CrossVenueSynchronizer(
        std::span<const VenueId> venue_ids,
        CrossVenueSynchronizerConfig config = CrossVenueSynchronizerConfig{});
    CrossVenueSynchronizer(std::span<const VenueId> venue_ids,
                           std::span<const VenuePair> venue_pairs,
                           CrossVenueSynchronizerConfig config =
                               CrossVenueSynchronizerConfig{});

    [[nodiscard]] SynchronizerUpdateStatus update(const FeatureSet& features) noexcept;
    [[nodiscard]] std::optional<std::reference_wrapper<const CrossVenueVenueState>>
    state(VenueId venue_id) const noexcept;
    [[nodiscard]] std::span<const CrossVenueVenueState> states() const noexcept;
    [[nodiscard]] std::optional<TimestampNs> latest_sample_timestamp_ns() const noexcept;
    [[nodiscard]] VenueFreshness freshness(VenueId venue_id,
                                           TimestampNs sample_timestamp_ns) const noexcept;
    [[nodiscard]] TimestampNs max_staleness_ns() const noexcept;
    [[nodiscard]] ConsolidatedReference
    consolidated_reference(TimestampNs sample_timestamp_ns) const noexcept;
    [[nodiscard]] bool venue_features(TimestampNs sample_timestamp_ns,
                                      std::span<VenueCrossFeatures> output) const noexcept;
    [[nodiscard]] std::size_t venue_count() const noexcept;
    [[nodiscard]] bool pairwise_features(TimestampNs sample_timestamp_ns,
                                         std::span<PairwiseCrossFeatures> output) const noexcept;
    [[nodiscard]] std::span<const VenuePair> venue_pairs() const noexcept;
    [[nodiscard]] std::size_t pair_count() const noexcept;

  private:
    CrossVenueSynchronizerConfig config_;
    std::vector<CrossVenueVenueState> states_;
    std::vector<VenuePair> venue_pairs_;
    std::optional<TimestampNs> latest_sample_timestamp_ns_;
};

} // namespace fairvaluelab
