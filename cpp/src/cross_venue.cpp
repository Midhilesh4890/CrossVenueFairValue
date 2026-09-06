#include "fairvaluelab/cross_venue.hpp"

#include <stdexcept>

fairvaluelab::CrossVenueSynchronizer::CrossVenueSynchronizer(
    const std::span<const VenueId> venue_ids) {
    if (venue_ids.empty()) {
        throw std::invalid_argument("at least one venue must be configured");
    }
    states_.reserve(venue_ids.size());
    for (const auto venue_id : venue_ids) {
        for (const auto& state : states_) {
            if (state.venue_id == venue_id) {
                throw std::invalid_argument("venue identifiers must be unique");
            }
        }
        CrossVenueVenueState state{};
        state.venue_id = venue_id;
        states_.push_back(state);
    }
}

fairvaluelab::SynchronizerUpdateStatus
fairvaluelab::CrossVenueSynchronizer::update(const FeatureSet& features) noexcept {
    if (features.local_receipt_timestamp_ns > features.sample_timestamp_ns) {
        return SynchronizerUpdateStatus::InvalidTimestamp;
    }
    if (latest_sample_timestamp_ns_.has_value() &&
        features.sample_timestamp_ns < *latest_sample_timestamp_ns_) {
        return SynchronizerUpdateStatus::OutOfOrder;
    }

    for (auto& state : states_) {
        if (state.venue_id != features.venue_id) {
            continue;
        }
        if (state.observed &&
            (features.sample_timestamp_ns < state.features.sample_timestamp_ns ||
             features.local_receipt_timestamp_ns < state.latest_local_receipt_timestamp_ns)) {
            return SynchronizerUpdateStatus::OutOfOrder;
        }
        state.observed = true;
        state.latest_local_receipt_timestamp_ns = features.local_receipt_timestamp_ns;
        state.latest_exchange_timestamp_ns = features.exchange_timestamp_ns;
        state.best_bid_ticks = features.best_bid_ticks;
        state.best_ask_ticks = features.best_ask_ticks;
        state.features = features;
        latest_sample_timestamp_ns_ = features.sample_timestamp_ns;
        return SynchronizerUpdateStatus::Accepted;
    }
    return SynchronizerUpdateStatus::UnknownVenue;
}

std::optional<std::reference_wrapper<const fairvaluelab::CrossVenueVenueState>>
fairvaluelab::CrossVenueSynchronizer::state(const VenueId venue_id) const noexcept {
    for (const auto& state : states_) {
        if (state.venue_id == venue_id) {
            return std::cref(state);
        }
    }
    return std::nullopt;
}

std::span<const fairvaluelab::CrossVenueVenueState>
fairvaluelab::CrossVenueSynchronizer::states() const noexcept {
    return states_;
}

std::optional<fairvaluelab::TimestampNs>
fairvaluelab::CrossVenueSynchronizer::latest_sample_timestamp_ns() const noexcept {
    return latest_sample_timestamp_ns_;
}
