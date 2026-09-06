#include "fairvaluelab/cross_venue.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>

namespace {

template <typename Value>
std::optional<double> optional_difference(const std::optional<Value>& first,
                                          const std::optional<Value>& second) noexcept {
    if (!first.has_value() || !second.has_value()) {
        return std::nullopt;
    }
    return static_cast<double>(*first) - static_cast<double>(*second);
}

std::optional<std::int64_t> timestamp_difference(const fairvaluelab::TimestampNs first,
                                                 const fairvaluelab::TimestampNs second) noexcept {
    constexpr auto maximum = static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
    if (first >= second) {
        const auto magnitude = first - second;
        return magnitude <= maximum ? std::optional{static_cast<std::int64_t>(magnitude)}
                                    : std::nullopt;
    }
    const auto magnitude = second - first;
    return magnitude <= maximum ? std::optional{-static_cast<std::int64_t>(magnitude)}
                                : std::nullopt;
}

} // namespace

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

fairvaluelab::CrossVenueSynchronizer::CrossVenueSynchronizer(
    const std::span<const VenueId> venue_ids, const std::span<const VenuePair> venue_pairs,
    const CrossVenueSynchronizerConfig config)
    : CrossVenueSynchronizer(venue_ids, config) {
    venue_pairs_.reserve(venue_pairs.size());
    for (const auto pair : venue_pairs) {
        if (pair.first == pair.second || !state(pair.first).has_value() ||
            !state(pair.second).has_value()) {
            throw std::invalid_argument("venue pair must contain two configured venues");
        }
        for (const auto configured : venue_pairs_) {
            if (configured.first == pair.first && configured.second == pair.second) {
                throw std::invalid_argument("venue pairs must be unique");
            }
        }
        venue_pairs_.push_back(pair);
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
        if (features.sample_kind == SampleKind::Event) {
            state.last_mid_move = 0;
            if (state.observed && state.features.mid_price.has_value() &&
                features.mid_price.has_value() && std::isfinite(*state.features.mid_price) &&
                std::isfinite(*features.mid_price)) {
                if (*features.mid_price > *state.features.mid_price) {
                    state.last_mid_move = 1;
                } else if (*features.mid_price < *state.features.mid_price) {
                    state.last_mid_move = -1;
                }
            }
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


bool fairvaluelab::CrossVenueSynchronizer::venue_features(
    const TimestampNs sample_timestamp_ns, const std::span<VenueCrossFeatures> output) const noexcept {
    if (output.size() != states_.size()) {
        return false;
    }
    const auto reference = consolidated_reference(sample_timestamp_ns);
    for (std::size_t index = 0; index < states_.size(); ++index) {
        const auto& state = states_[index];
        const auto venue_freshness = freshness(state.venue_id, sample_timestamp_ns);
        auto& features = output[index];
        features = {};
        features.venue_id = state.venue_id;
        features.observed = venue_freshness.observed;
        features.fresh = venue_freshness.usable;
        features.age_ns = venue_freshness.age_ns;
        if (venue_freshness.observed) {
            features.latest_local_receipt_timestamp_ns =
                state.latest_local_receipt_timestamp_ns;
            if (state.latest_exchange_timestamp_ns != 0) {
                features.latest_exchange_timestamp_ns = state.latest_exchange_timestamp_ns;
            }
        }
        if (!venue_freshness.usable) {
            continue;
        }

        const auto& source = state.features;
        if (source.mid_price.has_value() && reference.mid.has_value()) {
            features.mid_minus_consolidated_mid = *source.mid_price - *reference.mid;
        }
        if (source.microprice.has_value() && reference.microprice.has_value()) {
            features.microprice_minus_consolidated_microprice =
                *source.microprice - *reference.microprice;
        }
        features.spread_ticks = source.spread_ticks;
        features.imbalance_l1 = source.imbalance_l1;
        features.imbalance_l3 = source.imbalance_l3;
        features.imbalance_l5 = source.imbalance_l5;
        features.bid_depth = source.bid_depth;
        features.ask_depth = source.ask_depth;
        features.ofi_event_window = source.ofi_event_window;
        features.ofi_time_window = source.ofi_time_window;
        features.multi_level_ofi_event_window = source.multi_level_ofi_event_window;
        features.multi_level_ofi_time_window = source.multi_level_ofi_time_window;
        features.signed_trade_volume_event_window = source.signed_trade_volume_event_window;
        features.signed_trade_volume_time_window = source.signed_trade_volume_time_window;
        features.last_mid_move = state.last_mid_move;
    }
    return true;
}

std::size_t fairvaluelab::CrossVenueSynchronizer::venue_count() const noexcept {
    return states_.size();
}

bool fairvaluelab::CrossVenueSynchronizer::pairwise_features(
    const TimestampNs sample_timestamp_ns,
    const std::span<PairwiseCrossFeatures> output) const noexcept {
    if (output.size() != venue_pairs_.size()) {
        return false;
    }
    for (std::size_t index = 0; index < venue_pairs_.size(); ++index) {
        const auto pair = venue_pairs_[index];
        auto& features = output[index];
        features = {};
        features.venues = pair;
        const auto first_state = state(pair.first);
        const auto second_state = state(pair.second);
        if (!first_state.has_value() || !second_state.has_value() ||
            !freshness(pair.first, sample_timestamp_ns).usable ||
            !freshness(pair.second, sample_timestamp_ns).usable) {
            continue;
        }
        features.both_fresh = true;
        const auto& first = first_state->get();
        const auto& second = second_state->get();
        features.mid_difference =
            optional_difference(first.features.mid_price, second.features.mid_price);
        features.microprice_difference =
            optional_difference(first.features.microprice, second.features.microprice);
        features.imbalance_l1_difference =
            optional_difference(first.features.imbalance_l1, second.features.imbalance_l1);
        features.imbalance_l3_difference =
            optional_difference(first.features.imbalance_l3, second.features.imbalance_l3);
        features.imbalance_l5_difference =
            optional_difference(first.features.imbalance_l5, second.features.imbalance_l5);
        features.ofi_event_window_difference =
            first.features.ofi_event_window - second.features.ofi_event_window;
        features.ofi_time_window_difference =
            first.features.ofi_time_window - second.features.ofi_time_window;
        features.signed_trade_volume_event_window_difference =
            first.features.signed_trade_volume_event_window -
            second.features.signed_trade_volume_event_window;
        features.signed_trade_volume_time_window_difference =
            first.features.signed_trade_volume_time_window -
            second.features.signed_trade_volume_time_window;
        features.receipt_timestamp_difference_ns = timestamp_difference(
            first.latest_local_receipt_timestamp_ns, second.latest_local_receipt_timestamp_ns);
        if (first.latest_exchange_timestamp_ns != 0 && second.latest_exchange_timestamp_ns != 0) {
            features.exchange_timestamp_difference_ns = timestamp_difference(
                first.latest_exchange_timestamp_ns, second.latest_exchange_timestamp_ns);
        }
        features.last_mid_move_difference =
            static_cast<std::int16_t>(first.last_mid_move) -
            static_cast<std::int16_t>(second.last_mid_move);
    }
    return true;
}

std::span<const fairvaluelab::VenuePair>
fairvaluelab::CrossVenueSynchronizer::venue_pairs() const noexcept {
    return venue_pairs_;
}

std::size_t fairvaluelab::CrossVenueSynchronizer::pair_count() const noexcept {
    return venue_pairs_.size();
}
