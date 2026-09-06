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

class CrossVenueSynchronizer {
  public:
    explicit CrossVenueSynchronizer(
        std::span<const VenueId> venue_ids,
        CrossVenueSynchronizerConfig config = CrossVenueSynchronizerConfig{});

    [[nodiscard]] SynchronizerUpdateStatus update(const FeatureSet& features) noexcept;
    [[nodiscard]] std::optional<std::reference_wrapper<const CrossVenueVenueState>>
    state(VenueId venue_id) const noexcept;
    [[nodiscard]] std::span<const CrossVenueVenueState> states() const noexcept;
    [[nodiscard]] std::optional<TimestampNs> latest_sample_timestamp_ns() const noexcept;
    [[nodiscard]] VenueFreshness freshness(VenueId venue_id,
                                           TimestampNs sample_timestamp_ns) const noexcept;
    [[nodiscard]] TimestampNs max_staleness_ns() const noexcept;

  private:
    CrossVenueSynchronizerConfig config_;
    std::vector<CrossVenueVenueState> states_;
    std::optional<TimestampNs> latest_sample_timestamp_ns_;
};

} // namespace fairvaluelab
