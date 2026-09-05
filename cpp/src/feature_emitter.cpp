#include "fairvaluelab/feature_emitter.hpp"

#include "fairvaluelab/normalized_csv.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <istream>
#include <limits>
#include <optional>
#include <ostream>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace {

using fairvaluelab::FeatureSet;
using fairvaluelab::PriceLevel;
using fairvaluelab::Quantity;
using fairvaluelab::SampleKind;

Quantity total_depth(const std::span<const PriceLevel> levels) {
    Quantity total = 0;
    for (const auto& level : levels) {
        if (level.quantity > std::numeric_limits<Quantity>::max() - total) {
            return std::numeric_limits<Quantity>::max();
        }
        total += level.quantity;
    }
    return total;
}

std::optional<double> book_slope(const std::span<const PriceLevel> bids,
                                 const std::span<const PriceLevel> asks) {
    long double sum_x = 0.0L;
    long double sum_y = 0.0L;
    long double sum_xx = 0.0L;
    long double sum_xy = 0.0L;
    std::size_t count = 0;

    const auto add_side = [&](const std::span<const PriceLevel> levels) {
        if (levels.empty()) {
            return;
        }
        long double cumulative_quantity = 0.0L;
        const auto best_price = static_cast<long double>(levels.front().price_ticks);
        for (const auto& level : levels) {
            cumulative_quantity += static_cast<long double>(level.quantity);
            const auto distance =
                std::abs(static_cast<long double>(level.price_ticks) - best_price);
            sum_x += distance;
            sum_y += cumulative_quantity;
            sum_xx += distance * distance;
            sum_xy += distance * cumulative_quantity;
            ++count;
        }
    };

    add_side(bids);
    add_side(asks);
    if (count < 2) {
        return std::nullopt;
    }
    const auto samples = static_cast<long double>(count);
    const auto denominator = samples * sum_xx - sum_x * sum_x;
    if (denominator == 0.0L) {
        return std::nullopt;
    }
    return static_cast<double>((samples * sum_xy - sum_x * sum_y) / denominator);
}

long double bid_flow(const std::span<const PriceLevel> previous,
                     const std::span<const PriceLevel> current) {
    if (previous.empty()) {
        return current.empty() ? 0.0L : static_cast<long double>(current.front().quantity);
    }
    if (current.empty()) {
        return -static_cast<long double>(previous.front().quantity);
    }
    if (current.front().price_ticks > previous.front().price_ticks) {
        return static_cast<long double>(current.front().quantity);
    }
    if (current.front().price_ticks < previous.front().price_ticks) {
        return -static_cast<long double>(previous.front().quantity);
    }
    return static_cast<long double>(current.front().quantity) -
           static_cast<long double>(previous.front().quantity);
}

long double ask_flow(const std::span<const PriceLevel> previous,
                     const std::span<const PriceLevel> current) {
    if (previous.empty()) {
        return current.empty() ? 0.0L : -static_cast<long double>(current.front().quantity);
    }
    if (current.empty()) {
        return static_cast<long double>(previous.front().quantity);
    }
    if (current.front().price_ticks < previous.front().price_ticks) {
        return -static_cast<long double>(current.front().quantity);
    }
    if (current.front().price_ticks > previous.front().price_ticks) {
        return static_cast<long double>(previous.front().quantity);
    }
    return static_cast<long double>(previous.front().quantity) -
           static_cast<long double>(current.front().quantity);
}

std::optional<Quantity> quantity_at(const std::span<const PriceLevel> levels,
                                    const std::int64_t price) {
    for (const auto& level : levels) {
        if (level.price_ticks == price) {
            return level.quantity;
        }
    }
    return std::nullopt;
}

std::int64_t rounded_midpoint(const std::int64_t bid, const std::int64_t ask) {
    auto reference = bid / 2 + ask / 2;
    const auto remainder = bid % 2 + ask % 2;
    reference += remainder / 2;
    if (remainder % 2 > 0 && reference >= 0) {
        ++reference;
    } else if (remainder % 2 < 0 && reference <= 0) {
        --reference;
    }
    return reference;
}

std::uint64_t tick_distance(const std::int64_t price, const std::int64_t reference) {
    return price >= reference
               ? static_cast<std::uint64_t>(price) - static_cast<std::uint64_t>(reference)
               : static_cast<std::uint64_t>(reference) - static_cast<std::uint64_t>(price);
}

std::optional<long double> weighted_side_flow(const std::span<const PriceLevel> previous,
                                              const std::span<const PriceLevel> current,
                                              const std::int64_t reference,
                                              const std::uint64_t band_ticks,
                                              const long double sign) {
    std::optional<long double> value;
    const auto add_change = [&](const std::int64_t price, const long double change) {
        const auto distance = tick_distance(price, reference);
        if (distance <= band_ticks) {
            value = value.value_or(0.0L) + sign * change /
                        (1.0L + static_cast<long double>(distance));
        }
    };
    for (const auto& level : current) {
        const auto old_quantity = quantity_at(previous, level.price_ticks).value_or(0);
        add_change(level.price_ticks, static_cast<long double>(level.quantity) -
                                          static_cast<long double>(old_quantity));
    }
    for (const auto& level : previous) {
        if (!quantity_at(current, level.price_ticks).has_value()) {
            add_change(level.price_ticks, -static_cast<long double>(level.quantity));
        }
    }
    return value;
}

std::optional<double> multi_level_flow(const std::span<const PriceLevel> previous_bids,
                                       const std::span<const PriceLevel> previous_asks,
                                       const std::span<const PriceLevel> current_bids,
                                       const std::span<const PriceLevel> current_asks,
                                       const std::uint64_t band_ticks) {
    if (previous_bids.empty() || previous_asks.empty()) {
        return std::nullopt;
    }
    const auto reference = rounded_midpoint(previous_bids.front().price_ticks,
                                            previous_asks.front().price_ticks);
    const auto bids = weighted_side_flow(previous_bids, current_bids, reference, band_ticks, 1.0L);
    const auto asks = weighted_side_flow(previous_asks, current_asks, reference, band_ticks, -1.0L);
    if (!bids.has_value() && !asks.has_value()) {
        return std::nullopt;
    }
    return static_cast<double>(bids.value_or(0.0L) + asks.value_or(0.0L));
}

void snapshot(std::array<PriceLevel, fairvaluelab::BookSide::maximum_depth>& destination,
              std::size_t& count, const std::span<const PriceLevel> levels) {
    count = levels.size();
    std::copy_n(levels.begin(), count, destination.begin());
}

template <typename Value>
void write_optional(std::ostream& output, const std::optional<Value>& value) {
    if (value.has_value()) {
        output << *value;
    }
}

void write_row(std::ostream& output, const FeatureSet& features) {
    output << (features.sample_kind == SampleKind::Event ? "event" : "clock") << ','
           << features.venue_id << ',' << features.exchange_timestamp_ns << ','
           << features.local_receipt_timestamp_ns << ',' << features.sample_timestamp_ns << ',';
    write_optional(output, features.spread_ticks);
    output << ',';
    write_optional(output, features.mid_price);
    output << ',';
    write_optional(output, features.microprice);
    output << ',';
    write_optional(output, features.imbalance_l1);
    output << ',';
    write_optional(output, features.imbalance_l3);
    output << ',';
    write_optional(output, features.imbalance_l5);
    output << ',' << features.bid_depth << ',' << features.ask_depth << ',';
    write_optional(output, features.book_slope);
    output << ',' << features.time_since_last_update_ns << ',' << features.ofi_event_window << ','
           << features.ofi_time_window << ',';
    write_optional(output, features.multi_level_ofi_event_window);
    output << ',';
    write_optional(output, features.multi_level_ofi_time_window);
    output << ',' << features.signed_trade_volume_event_window << ','
           << features.signed_trade_volume_time_window << ','
           << features.trade_count_event_window << ',' << features.trade_count_time_window << ',';
    write_optional(output, features.trade_vwap_deviation_event_window);
    output << ',';
    write_optional(output, features.trade_vwap_deviation_time_window);
    output << '\n';
}

}

fairvaluelab::FeatureSet fairvaluelab::compute_features(
    const OrderBook& book, const VenueId venue_id, const TimestampNs exchange_timestamp_ns,
    const TimestampNs local_receipt_timestamp_ns, const TimestampNs sample_timestamp_ns,
    const SampleKind sample_kind) {
    if (sample_timestamp_ns < local_receipt_timestamp_ns) {
        throw std::invalid_argument("sample timestamp precedes the last update");
    }
    return FeatureSet{
        sample_kind,
        venue_id,
        exchange_timestamp_ns,
        local_receipt_timestamp_ns,
        sample_timestamp_ns,
        book.spread(),
        book.mid_price(),
        book.microprice(),
        book.depth_imbalance(1),
        book.depth_imbalance(3),
        book.depth_imbalance(5),
        total_depth(book.bids()),
        total_depth(book.asks()),
        book_slope(book.bids(), book.asks()),
        sample_timestamp_ns - local_receipt_timestamp_ns,
        0.0,
        0.0,
        std::nullopt,
        std::nullopt,
        0.0,
        0.0,
        0,
        0,
        std::nullopt,
        std::nullopt,
    };
}

fairvaluelab::FeatureEmitter::FeatureEmitter(const TimestampNs clock_interval_ns)
    : FeatureEmitter(FeatureEmitterConfig{.clock_interval_ns = clock_interval_ns}) {}

fairvaluelab::FeatureEmitter::FeatureEmitter(FeatureEmitterConfig config) : config_(config) {
    if (config_.clock_interval_ns == 0 || config_.event_window == 0 ||
        config_.time_window_ns == 0 || config_.band_ticks == 0 || config_.venue_capacity == 0) {
        throw std::invalid_argument("feature emitter configuration must be positive");
    }
    if (config_.event_window > maximum_event_window) {
        throw std::invalid_argument("event window exceeds maximum history capacity");
    }
    states_.reserve(config_.venue_capacity);
}

fairvaluelab::FeatureEmitter::FeatureEmitter(const FeatureEmitter& other)
    : FeatureEmitter(other.config_) {
    current_timestamp_ns_ = other.current_timestamp_ns_;
    states_.assign(other.states_.begin(), other.states_.end());
}

fairvaluelab::FeatureEmitter&
fairvaluelab::FeatureEmitter::operator=(const FeatureEmitter& other) {
    if (this != &other) {
        FeatureEmitter replacement{other};
        *this = std::move(replacement);
    }
    return *this;
}

fairvaluelab::FeatureEmitter::VenueState&
fairvaluelab::FeatureEmitter::venue_state(const VenueId venue_id) {
    for (auto& state : states_) {
        if (state.venue_id == venue_id) {
            return state;
        }
    }
    if (states_.capacity() < config_.venue_capacity) {
        throw std::runtime_error("feature emitter has no reserved venue storage");
    }
    if (states_.size() == config_.venue_capacity) {
        throw std::runtime_error("feature emitter venue capacity exceeded");
    }
    return states_.emplace_back(venue_id, config_.event_window);
}

std::optional<fairvaluelab::FeatureEmitterDroppedEntries>
fairvaluelab::FeatureEmitter::dropped_entries(const VenueId venue_id) const {
    for (const auto& state : states_) {
        if (state.venue_id == venue_id) {
            return FeatureEmitterDroppedEntries{venue_id, state.order_flow.dropped_entries(),
                                                 state.trade_flow.dropped_entries()};
        }
    }
    return std::nullopt;
}

std::vector<fairvaluelab::FeatureEmitterDroppedEntries>
fairvaluelab::FeatureEmitter::dropped_entries() const {
    std::vector<FeatureEmitterDroppedEntries> output;
    output.reserve(states_.size());
    for (const auto& state : states_) {
        output.push_back(FeatureEmitterDroppedEntries{
            state.venue_id, state.order_flow.dropped_entries(), state.trade_flow.dropped_entries()});
    }
    return output;
}

fairvaluelab::FeatureSet fairvaluelab::FeatureEmitter::features(
    const VenueState& state, const VenueId venue_id, const TimestampNs exchange_timestamp_ns,
    const TimestampNs local_receipt_timestamp_ns, const TimestampNs sample_timestamp_ns,
    const SampleKind sample_kind) const {
    auto output = compute_features(state.book, venue_id, exchange_timestamp_ns,
                                   local_receipt_timestamp_ns, sample_timestamp_ns, sample_kind);
    const auto cutoff = sample_timestamp_ns > config_.time_window_ns
                            ? sample_timestamp_ns - config_.time_window_ns
                            : 0;

    for (std::size_t index = 0; index < state.order_flow.size(); ++index) {
        const auto& item = state.order_flow[index];
        output.ofi_event_window += item.value;
        if (item.multi_level_value.has_value()) {
            output.multi_level_ofi_event_window =
                output.multi_level_ofi_event_window.value_or(0.0) + *item.multi_level_value;
        }
        if (item.timestamp_ns >= cutoff) {
            output.ofi_time_window += item.value;
            if (item.multi_level_value.has_value()) {
                output.multi_level_ofi_time_window =
                    output.multi_level_ofi_time_window.value_or(0.0) + *item.multi_level_value;
            }
        }
    }

    double event_weighted_deviation = 0.0;
    double event_deviation_volume = 0.0;
    for (std::size_t index = 0; index < state.trade_flow.size(); ++index) {
        const auto& item = state.trade_flow[index];
        output.signed_trade_volume_event_window += item.signed_volume;
        ++output.trade_count_event_window;
        if (item.price_deviation.has_value()) {
            event_weighted_deviation += item.volume * *item.price_deviation;
            event_deviation_volume += item.volume;
        }
    }
    if (event_deviation_volume > 0.0) {
        output.trade_vwap_deviation_event_window =
            event_weighted_deviation / event_deviation_volume;
    }

    double time_weighted_deviation = 0.0;
    double time_deviation_volume = 0.0;
    for (std::size_t index = 0; index < state.trade_flow.size(); ++index) {
        const auto& item = state.trade_flow[index];
        if (item.timestamp_ns < cutoff) {
            continue;
        }
        output.signed_trade_volume_time_window += item.signed_volume;
        ++output.trade_count_time_window;
        if (item.price_deviation.has_value()) {
            time_weighted_deviation += item.volume * *item.price_deviation;
            time_deviation_volume += item.volume;
        }
    }
    if (time_deviation_volume > 0.0) {
        output.trade_vwap_deviation_time_window =
            time_weighted_deviation / time_deviation_volume;
    }
    return output;
}

void fairvaluelab::FeatureEmitter::advance(const TimestampNs timestamp_ns,
                                            std::vector<FeatureSet>& output) {
    if (current_timestamp_ns_.has_value() && timestamp_ns < *current_timestamp_ns_) {
        throw std::invalid_argument("events must be ordered by local receipt timestamp");
    }

    const auto begin = output.size();
    for (auto& state : states_) {
        while (state.initialized && state.next_clock_timestamp_ns <= timestamp_ns) {
            output.push_back(features(state, state.venue_id, state.exchange_timestamp_ns,
                                      state.local_receipt_timestamp_ns,
                                      state.next_clock_timestamp_ns, SampleKind::Clock));
            state.next_clock_timestamp_ns += config_.clock_interval_ns;
        }
    }
    current_timestamp_ns_ = timestamp_ns;
    std::sort(output.begin() + static_cast<std::ptrdiff_t>(begin), output.end(), [](const FeatureSet& lhs,
                                                      const FeatureSet& rhs) {
        if (lhs.sample_timestamp_ns != rhs.sample_timestamp_ns) {
            return lhs.sample_timestamp_ns < rhs.sample_timestamp_ns;
        }
        return lhs.venue_id < rhs.venue_id;
    });
}

std::vector<fairvaluelab::FeatureSet>
fairvaluelab::FeatureEmitter::process(const BookUpdate& update) {
    std::vector<FeatureSet> output;
    static_cast<void>(process(update, output));
    return output;
}

fairvaluelab::ApplyResult fairvaluelab::FeatureEmitter::process(
    const BookUpdate& update, std::vector<FeatureSet>& output) {
    advance(update.local_receipt_timestamp_ns, output);
    auto& state = venue_state(update.venue_id);
    const std::span previous_bids{state.previous_bids.data(), state.previous_bid_count};
    const std::span previous_asks{state.previous_asks.data(), state.previous_ask_count};
    const auto result = state.book.apply(update);
    if (result.accepted()) {
        const auto current_bids = state.book.bids();
        const auto current_asks = state.book.asks();
        const auto value = static_cast<double>(bid_flow(previous_bids, current_bids) +
                                               ask_flow(previous_asks, current_asks));
        const auto multi_value = multi_level_flow(previous_bids, previous_asks, current_bids,
                                                   current_asks, config_.band_ticks);
        snapshot(state.previous_bids, state.previous_bid_count, current_bids);
        snapshot(state.previous_asks, state.previous_ask_count, current_asks);
        state.order_flow.push_back(
            VenueState::OrderFlow{update.local_receipt_timestamp_ns, value, multi_value});
        state.exchange_timestamp_ns = update.exchange_timestamp_ns;
        state.local_receipt_timestamp_ns = update.local_receipt_timestamp_ns;
        if (!state.initialized) {
            state.initialized = true;
            state.next_clock_timestamp_ns = update.local_receipt_timestamp_ns +
                                                    config_.clock_interval_ns -
                                                    update.local_receipt_timestamp_ns %
                                                        config_.clock_interval_ns;
        }
        output.push_back(features(state, update.venue_id, update.exchange_timestamp_ns,
                                  update.local_receipt_timestamp_ns,
                                  update.local_receipt_timestamp_ns, SampleKind::Event));
    }
    return result;
}

std::vector<fairvaluelab::FeatureSet>
fairvaluelab::FeatureEmitter::process(const Trade& trade) {
    std::vector<FeatureSet> output;
    process(trade, output);
    return output;
}

void fairvaluelab::FeatureEmitter::process(const Trade& trade, std::vector<FeatureSet>& output) {
    advance(trade.local_receipt_timestamp_ns, output);
    auto& state = venue_state(trade.venue_id);
    const auto mid = state.book.mid_price();
    const auto volume = static_cast<double>(trade.quantity);
    state.trade_flow.push_back(VenueState::TradeFlow{
        trade.local_receipt_timestamp_ns,
        trade.side == TradeSide::Buy ? volume : -volume,
        volume,
        mid.has_value() ? std::optional{static_cast<double>(trade.price_ticks) - *mid}
                        : std::nullopt,
    });
    state.exchange_timestamp_ns = trade.exchange_timestamp_ns;
    state.local_receipt_timestamp_ns = trade.local_receipt_timestamp_ns;
    if (!state.initialized) {
        state.initialized = true;
        state.next_clock_timestamp_ns = trade.local_receipt_timestamp_ns +
                                                config_.clock_interval_ns -
                                                trade.local_receipt_timestamp_ns %
                                                    config_.clock_interval_ns;
    }
    output.push_back(features(state, trade.venue_id, trade.exchange_timestamp_ns,
                              trade.local_receipt_timestamp_ns,
                              trade.local_receipt_timestamp_ns, SampleKind::Event));
}

std::uint64_t fairvaluelab::write_feature_csv(std::istream& input, std::ostream& output,
                                              const TimestampNs clock_interval_ns) {
    return write_feature_csv(input, output,
                             FeatureEmitterConfig{.clock_interval_ns = clock_interval_ns});
}

std::uint64_t fairvaluelab::write_feature_csv(std::istream& input, std::ostream& output,
                                              const FeatureEmitterConfig config) {
    FeatureEmitter emitter{config};
    return write_feature_csv(input, output, emitter);
}

std::uint64_t fairvaluelab::write_feature_csv(std::istream& input, std::ostream& output,
                                              FeatureEmitter& emitter) {
    std::string line;
    if (!std::getline(input, line) || !is_normalized_csv_header(line)) {
        throw std::runtime_error("invalid normalized log header");
    }

    output << "sample_kind,venue_id,exchange_timestamp_ns,local_receipt_timestamp_ns,"
              "sample_timestamp_ns,spread_ticks,mid_price,microprice,imbalance_l1,imbalance_l3,"
              "imbalance_l5,bid_depth,ask_depth,book_slope,time_since_last_update_ns,"
              "ofi_event_window,ofi_time_window,multi_level_ofi_event_window,"
              "multi_level_ofi_time_window,signed_trade_volume_event_window,"
              "signed_trade_volume_time_window,trade_count_event_window,trade_count_time_window,"
              "trade_vwap_deviation_event_window,trade_vwap_deviation_time_window\n";
    output << std::setprecision(17);
    std::uint64_t row_count = 0;
    std::size_t line_number = 1;
    while (std::getline(input, line)) {
        ++line_number;
        if (line.empty()) {
            continue;
        }
        try {
            const auto event = parse_normalized_event(line);
            const auto emitted = std::holds_alternative<BookUpdate>(event)
                                     ? emitter.process(std::get<BookUpdate>(event))
                                     : emitter.process(std::get<Trade>(event));
            for (const auto& row : emitted) {
                write_row(output, row);
                ++row_count;
            }
        } catch (const std::exception& error) {
            throw std::runtime_error("invalid normalized event on line " +
                                     std::to_string(line_number) + ": " + error.what());
        }
    }
    if (!output) {
        throw std::runtime_error("failed to write feature output");
    }
    return row_count;
}
