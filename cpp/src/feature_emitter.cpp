#include "fairvaluelab/feature_emitter.hpp"

#include "fairvaluelab/normalized_csv.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <istream>
#include <limits>
#include <ostream>
#include <span>
#include <stdexcept>
#include <string>
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
    output << ',' << features.time_since_last_update_ns << '\n';
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
    };
}

fairvaluelab::FeatureEmitter::FeatureEmitter(const TimestampNs clock_interval_ns)
    : clock_interval_ns_(clock_interval_ns) {
    if (clock_interval_ns == 0) {
        throw std::invalid_argument("clock interval must be positive");
    }
}

std::vector<fairvaluelab::FeatureSet>
fairvaluelab::FeatureEmitter::process(const BookUpdate& update) {
    if (current_timestamp_ns_.has_value() &&
        update.local_receipt_timestamp_ns < *current_timestamp_ns_) {
        throw std::invalid_argument("updates must be ordered by local receipt timestamp");
    }

    std::vector<FeatureSet> features;
    for (auto& [venue_id, state] : states_) {
        while (state.initialized &&
               state.next_clock_timestamp_ns <= update.local_receipt_timestamp_ns) {
            features.push_back(compute_features(
                state.book, venue_id, state.exchange_timestamp_ns,
                state.local_receipt_timestamp_ns, state.next_clock_timestamp_ns,
                SampleKind::Clock));
            state.next_clock_timestamp_ns += clock_interval_ns_;
        }
    }

    auto [state, inserted] = states_.try_emplace(update.venue_id);
    static_cast<void>(inserted);
    if (state->second.book.apply(update).accepted()) {
        state->second.exchange_timestamp_ns = update.exchange_timestamp_ns;
        state->second.local_receipt_timestamp_ns = update.local_receipt_timestamp_ns;
        if (!state->second.initialized) {
            state->second.initialized = true;
            state->second.next_clock_timestamp_ns = update.local_receipt_timestamp_ns +
                                                    clock_interval_ns_ -
                                                    update.local_receipt_timestamp_ns %
                                                        clock_interval_ns_;
        }
        features.push_back(compute_features(
            state->second.book, update.venue_id, update.exchange_timestamp_ns,
            update.local_receipt_timestamp_ns, update.local_receipt_timestamp_ns,
            SampleKind::Event));
    }
    current_timestamp_ns_ = update.local_receipt_timestamp_ns;

    std::stable_sort(features.begin(), features.end(), [](const FeatureSet& lhs,
                                                          const FeatureSet& rhs) {
        if (lhs.sample_timestamp_ns != rhs.sample_timestamp_ns) {
            return lhs.sample_timestamp_ns < rhs.sample_timestamp_ns;
        }
        if (lhs.sample_kind != rhs.sample_kind) {
            return lhs.sample_kind == SampleKind::Clock;
        }
        return lhs.venue_id < rhs.venue_id;
    });
    return features;
}

std::uint64_t fairvaluelab::write_feature_csv(std::istream& input, std::ostream& output,
                                              const TimestampNs clock_interval_ns) {
    std::string line;
    if (!std::getline(input, line) || !is_normalized_csv_header(line)) {
        throw std::runtime_error("invalid normalized log header");
    }

    output << "sample_kind,venue_id,exchange_timestamp_ns,local_receipt_timestamp_ns,"
              "sample_timestamp_ns,spread_ticks,mid_price,microprice,imbalance_l1,imbalance_l3,"
              "imbalance_l5,bid_depth,ask_depth,book_slope,time_since_last_update_ns\n";
    output << std::setprecision(17);
    FeatureEmitter emitter{clock_interval_ns};
    std::uint64_t row_count = 0;
    std::size_t line_number = 1;
    while (std::getline(input, line)) {
        ++line_number;
        if (line.empty()) {
            continue;
        }
        try {
            for (const auto& features : emitter.process(parse_normalized_event(line))) {
                write_row(output, features);
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
