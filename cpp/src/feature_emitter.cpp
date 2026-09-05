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
                                    const std::size_t depth, const std::int64_t price) {
    const auto count = std::min(depth, levels.size());
    for (std::size_t index = 0; index < count; ++index) {
        if (levels[index].price_ticks == price) {
            return levels[index].quantity;
        }
    }
    return std::nullopt;
}

long double weighted_side_flow(const std::span<const PriceLevel> previous,
                               const std::span<const PriceLevel> current,
                               const std::size_t depth, const long double sign) {
    long double value = 0.0L;
    const auto current_count = std::min(depth, current.size());
    for (std::size_t index = 0; index < current_count; ++index) {
        const auto old_quantity = quantity_at(previous, depth, current[index].price_ticks).value_or(0);
        const auto change = static_cast<long double>(current[index].quantity) -
                            static_cast<long double>(old_quantity);
        value += sign * change / static_cast<long double>(index + 1);
    }

    const auto previous_count = std::min(depth, previous.size());
    for (std::size_t index = 0; index < previous_count; ++index) {
        if (!quantity_at(current, depth, previous[index].price_ticks).has_value()) {
            value -= sign * static_cast<long double>(previous[index].quantity) /
                     static_cast<long double>(index + 1);
        }
    }
    return value;
}

double multi_level_flow(const std::span<const PriceLevel> previous_bids,
                        const std::span<const PriceLevel> previous_asks,
                        const std::span<const PriceLevel> current_bids,
                        const std::span<const PriceLevel> current_asks,
                        const std::size_t depth) {
    return static_cast<double>(weighted_side_flow(previous_bids, current_bids, depth, 1.0L) +
                               weighted_side_flow(previous_asks, current_asks, depth, -1.0L));
}

void snapshot(std::array<PriceLevel, fairvaluelab::BookSide::maximum_depth>& destination,
              std::size_t& count, const std::span<const PriceLevel> levels,
              const std::size_t depth) {
    count = std::min({depth, levels.size(), destination.size()});
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
           << features.ofi_time_window << ',' << features.multi_level_ofi_event_window << ','
           << features.multi_level_ofi_time_window << ','
           << features.signed_trade_volume_event_window << ','
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
        0.0,
        0.0,
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
        config_.time_window_ns == 0 || config_.multi_level_depth == 0) {
        throw std::invalid_argument("feature emitter configuration must be positive");
    }
}

void fairvaluelab::FeatureEmitter::trim(VenueState& state,
                                        const TimestampNs timestamp_ns) const {
    const auto cutoff =
        timestamp_ns > config_.time_window_ns ? timestamp_ns - config_.time_window_ns : 0;
    while (state.order_flow.size() > config_.event_window &&
           state.order_flow.front().timestamp_ns < cutoff) {
        state.order_flow.pop_front();
    }
    while (state.trade_flow.size() > config_.event_window &&
           state.trade_flow.front().timestamp_ns < cutoff) {
        state.trade_flow.pop_front();
    }
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

    const auto order_event_begin =
        state.order_flow.size() > config_.event_window
            ? state.order_flow.end() - static_cast<std::ptrdiff_t>(config_.event_window)
            : state.order_flow.begin();
    for (auto item = order_event_begin; item != state.order_flow.end(); ++item) {
        output.ofi_event_window += item->value;
        output.multi_level_ofi_event_window += item->multi_level_value;
    }
    for (const auto& item : state.order_flow) {
        if (item.timestamp_ns >= cutoff) {
            output.ofi_time_window += item.value;
            output.multi_level_ofi_time_window += item.multi_level_value;
        }
    }

    double event_weighted_deviation = 0.0;
    double event_deviation_volume = 0.0;
    const auto trade_event_begin =
        state.trade_flow.size() > config_.event_window
            ? state.trade_flow.end() - static_cast<std::ptrdiff_t>(config_.event_window)
            : state.trade_flow.begin();
    for (auto item = trade_event_begin; item != state.trade_flow.end(); ++item) {
        output.signed_trade_volume_event_window += item->signed_volume;
        ++output.trade_count_event_window;
        if (item->price_deviation.has_value()) {
            event_weighted_deviation += item->volume * *item->price_deviation;
            event_deviation_volume += item->volume;
        }
    }
    if (event_deviation_volume > 0.0) {
        output.trade_vwap_deviation_event_window =
            event_weighted_deviation / event_deviation_volume;
    }

    double time_weighted_deviation = 0.0;
    double time_deviation_volume = 0.0;
    for (const auto& item : state.trade_flow) {
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

std::vector<fairvaluelab::FeatureSet>
fairvaluelab::FeatureEmitter::advance(const TimestampNs timestamp_ns) {
    if (current_timestamp_ns_.has_value() && timestamp_ns < *current_timestamp_ns_) {
        throw std::invalid_argument("events must be ordered by local receipt timestamp");
    }

    std::vector<FeatureSet> output;
    for (auto& [venue_id, state] : states_) {
        while (state.initialized && state.next_clock_timestamp_ns <= timestamp_ns) {
            trim(state, state.next_clock_timestamp_ns);
            output.push_back(features(state, venue_id, state.exchange_timestamp_ns,
                                      state.local_receipt_timestamp_ns,
                                      state.next_clock_timestamp_ns, SampleKind::Clock));
            state.next_clock_timestamp_ns += config_.clock_interval_ns;
        }
    }
    current_timestamp_ns_ = timestamp_ns;
    std::stable_sort(output.begin(), output.end(), [](const FeatureSet& lhs,
                                                      const FeatureSet& rhs) {
        if (lhs.sample_timestamp_ns != rhs.sample_timestamp_ns) {
            return lhs.sample_timestamp_ns < rhs.sample_timestamp_ns;
        }
        return lhs.venue_id < rhs.venue_id;
    });
    return output;
}

std::vector<fairvaluelab::FeatureSet>
fairvaluelab::FeatureEmitter::process(const BookUpdate& update) {
    auto output = advance(update.local_receipt_timestamp_ns);
    auto [state, inserted] = states_.try_emplace(update.venue_id);
    static_cast<void>(inserted);
    auto& venue_state = state->second;
    const std::span previous_bids{venue_state.previous_bids.data(), venue_state.previous_bid_count};
    const std::span previous_asks{venue_state.previous_asks.data(), venue_state.previous_ask_count};
    if (venue_state.book.apply(update).accepted()) {
        const auto current_bids = venue_state.book.bids();
        const auto current_asks = venue_state.book.asks();
        const auto value = static_cast<double>(bid_flow(previous_bids, current_bids) +
                                               ask_flow(previous_asks, current_asks));
        const auto multi_value = multi_level_flow(previous_bids, previous_asks, current_bids,
                                                   current_asks, config_.multi_level_depth);
        snapshot(venue_state.previous_bids, venue_state.previous_bid_count, current_bids,
                 config_.multi_level_depth);
        snapshot(venue_state.previous_asks, venue_state.previous_ask_count, current_asks,
                 config_.multi_level_depth);
        state->second.order_flow.push_back(
            VenueState::OrderFlow{update.local_receipt_timestamp_ns, value, multi_value});
        state->second.exchange_timestamp_ns = update.exchange_timestamp_ns;
        state->second.local_receipt_timestamp_ns = update.local_receipt_timestamp_ns;
        if (!state->second.initialized) {
            state->second.initialized = true;
            state->second.next_clock_timestamp_ns = update.local_receipt_timestamp_ns +
                                                    config_.clock_interval_ns -
                                                    update.local_receipt_timestamp_ns %
                                                        config_.clock_interval_ns;
        }
        trim(state->second, update.local_receipt_timestamp_ns);
        output.push_back(features(state->second, update.venue_id, update.exchange_timestamp_ns,
                                  update.local_receipt_timestamp_ns,
                                  update.local_receipt_timestamp_ns, SampleKind::Event));
    }
    return output;
}

std::vector<fairvaluelab::FeatureSet>
fairvaluelab::FeatureEmitter::process(const Trade& trade) {
    auto output = advance(trade.local_receipt_timestamp_ns);
    auto [state, inserted] = states_.try_emplace(trade.venue_id);
    static_cast<void>(inserted);
    const auto mid = state->second.book.mid_price();
    const auto volume = static_cast<double>(trade.quantity);
    state->second.trade_flow.push_back(VenueState::TradeFlow{
        trade.local_receipt_timestamp_ns,
        trade.side == TradeSide::Buy ? volume : -volume,
        volume,
        mid.has_value() ? std::optional{static_cast<double>(trade.price_ticks) - *mid}
                        : std::nullopt,
    });
    state->second.exchange_timestamp_ns = trade.exchange_timestamp_ns;
    state->second.local_receipt_timestamp_ns = trade.local_receipt_timestamp_ns;
    if (!state->second.initialized) {
        state->second.initialized = true;
        state->second.next_clock_timestamp_ns = trade.local_receipt_timestamp_ns +
                                                config_.clock_interval_ns -
                                                trade.local_receipt_timestamp_ns %
                                                    config_.clock_interval_ns;
    }
    trim(state->second, trade.local_receipt_timestamp_ns);
    output.push_back(features(state->second, trade.venue_id, trade.exchange_timestamp_ns,
                              trade.local_receipt_timestamp_ns,
                              trade.local_receipt_timestamp_ns, SampleKind::Event));
    return output;
}

std::uint64_t fairvaluelab::write_feature_csv(std::istream& input, std::ostream& output,
                                              const TimestampNs clock_interval_ns) {
    return write_feature_csv(input, output,
                             FeatureEmitterConfig{.clock_interval_ns = clock_interval_ns});
}

std::uint64_t fairvaluelab::write_feature_csv(std::istream& input, std::ostream& output,
                                              const FeatureEmitterConfig config) {
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
    FeatureEmitter emitter{config};
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
