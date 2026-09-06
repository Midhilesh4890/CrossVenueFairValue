#include "fairvaluelab/cross_venue.hpp"

#include <cmath>
#include <stdexcept>

fairvaluelab::CrossVenueSynchronizer::CrossVenueSynchronizer(
    const std::span<const VenueId> venue_ids, const CrossVenueSynchronizerConfig config)
    : config_(config) {
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

fairvaluelab::VenueFreshness
fairvaluelab::CrossVenueSynchronizer::freshness(const VenueId venue_id,
                                                const TimestampNs sample_timestamp_ns) const noexcept {
    const auto venue_state = state(venue_id);
    if (!venue_state.has_value() || !venue_state->get().observed) {
        return {};
    }
    const auto receipt_timestamp = venue_state->get().latest_local_receipt_timestamp_ns;
    if (sample_timestamp_ns < receipt_timestamp) {
        return VenueFreshness{.observed = true, .usable = false, .age_ns = std::nullopt};
    }
    const auto age = sample_timestamp_ns - receipt_timestamp;
    return VenueFreshness{.observed = true,
                          .usable = age <= config_.max_staleness_ns,
                          .age_ns = age};
}

fairvaluelab::TimestampNs
fairvaluelab::CrossVenueSynchronizer::max_staleness_ns() const noexcept {
    return config_.max_staleness_ns;
}

fairvaluelab::ConsolidatedReference
fairvaluelab::CrossVenueSynchronizer::consolidated_reference(
    const TimestampNs sample_timestamp_ns) const noexcept {
    ConsolidatedReference output{};
    output.sample_timestamp_ns = sample_timestamp_ns;
    double mid_sum = 0.0;
    double microprice_sum = 0.0;
    for (const auto& venue : states_) {
        const auto venue_freshness = freshness(venue.venue_id, sample_timestamp_ns);
        if (!venue_freshness.usable || !venue.best_bid_ticks.has_value() ||
            !venue.best_ask_ticks.has_value() || *venue.best_bid_ticks > *venue.best_ask_ticks) {
            continue;
        }
        ++output.valid_venue_count;
        if (venue.features.mid_price.has_value() && std::isfinite(*venue.features.mid_price)) {
            mid_sum += *venue.features.mid_price;
            ++output.mid_venue_count;
        }
        if (venue.features.microprice.has_value() &&
            std::isfinite(*venue.features.microprice)) {
            microprice_sum += *venue.features.microprice;
            ++output.microprice_venue_count;
        }
    }
    if (output.mid_venue_count != 0) {
        output.mid = mid_sum / static_cast<double>(output.mid_venue_count);
    }
    if (output.microprice_venue_count != 0) {
        output.microprice =
            microprice_sum / static_cast<double>(output.microprice_venue_count);
    }
    return output;
}
