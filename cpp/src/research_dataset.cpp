#include "fairvaluelab/research_dataset.hpp"

#include "fairvaluelab/normalized_csv.hpp"

#include <algorithm>
#include <iomanip>
#include <istream>
#include <optional>
#include <ostream>
#include <span>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>

namespace {

using fairvaluelab::CrossVenueSample;
using fairvaluelab::FutureFairValueTarget;
using fairvaluelab::NormalizedEvent;
using fairvaluelab::PairwiseCrossFeatures;
using fairvaluelab::SampleKind;
using fairvaluelab::TimestampNs;
using fairvaluelab::VenueCrossFeatures;
using fairvaluelab::VenueId;
using fairvaluelab::VenuePair;

template <typename Value>
void write_optional(std::ostream& output, const std::optional<Value>& value) {
    if (value.has_value()) {
        output << *value;
    }
}

void write_optional_direction(std::ostream& output, const std::optional<std::int8_t>& value) {
    if (value.has_value()) {
        output << static_cast<int>(*value);
    }
}

std::string venue_prefix(const VenueId venue_id) {
    return "venue_" + std::to_string(venue_id) + '_';
}

std::string pair_prefix(const VenuePair pair) {
    return "pair_" + std::to_string(pair.first) + '_' + std::to_string(pair.second) + '_';
}

void write_venue_header(std::ostream& output, const VenueId venue_id) {
    const auto prefix = venue_prefix(venue_id);
    output << ',' << prefix << "observed," << prefix << "fresh," << prefix << "age_ns," << prefix
           << "latest_local_receipt_timestamp_ns," << prefix << "latest_exchange_timestamp_ns,"
           << prefix << "mid_minus_consolidated_mid," << prefix
           << "microprice_minus_consolidated_microprice," << prefix << "spread_ticks," << prefix
           << "imbalance_l1," << prefix << "imbalance_l3," << prefix << "imbalance_l5," << prefix
           << "bid_depth," << prefix << "ask_depth," << prefix << "ofi_event_window," << prefix
           << "ofi_time_window," << prefix << "multi_level_ofi_event_window," << prefix
           << "multi_level_ofi_time_window," << prefix
           << "signed_trade_volume_event_window," << prefix
           << "signed_trade_volume_time_window," << prefix << "last_mid_move";
}

void write_pair_header(std::ostream& output, const VenuePair pair) {
    const auto prefix = pair_prefix(pair);
    output << ',' << prefix << "both_fresh," << prefix << "mid_difference," << prefix
           << "microprice_difference," << prefix << "imbalance_l1_difference," << prefix
           << "imbalance_l3_difference," << prefix << "imbalance_l5_difference," << prefix
           << "ofi_event_window_difference," << prefix << "ofi_time_window_difference," << prefix
           << "signed_trade_volume_event_window_difference," << prefix
           << "signed_trade_volume_time_window_difference," << prefix
           << "receipt_timestamp_difference_ns," << prefix << "exchange_timestamp_difference_ns,"
           << prefix << "last_mid_move_difference";
}

void write_target_header(std::ostream& output, const TimestampNs horizon) {
    const auto suffix = '_' + std::to_string(horizon);
    output << ",target_timestamp" << suffix << ",target_delay_ns" << suffix
           << ",future_consolidated_mid" << suffix << ",future_consolidated_microprice" << suffix
           << ",mid_return" << suffix << ",microprice_return" << suffix << ",mid_direction"
           << suffix << ",microprice_direction" << suffix;
}

void write_venue(std::ostream& output, const VenueCrossFeatures& venue) {
    output << ',' << (venue.observed ? 1 : 0) << ',' << (venue.fresh ? 1 : 0) << ',';
    write_optional(output, venue.age_ns);
    output << ',';
    write_optional(output, venue.latest_local_receipt_timestamp_ns);
    output << ',';
    write_optional(output, venue.latest_exchange_timestamp_ns);
    output << ',';
    write_optional(output, venue.mid_minus_consolidated_mid);
    output << ',';
    write_optional(output, venue.microprice_minus_consolidated_microprice);
    output << ',';
    write_optional(output, venue.spread_ticks);
    output << ',';
    write_optional(output, venue.imbalance_l1);
    output << ',';
    write_optional(output, venue.imbalance_l3);
    output << ',';
    write_optional(output, venue.imbalance_l5);
    output << ',';
    write_optional(output, venue.bid_depth);
    output << ',';
    write_optional(output, venue.ask_depth);
    output << ',';
    write_optional(output, venue.ofi_event_window);
    output << ',';
    write_optional(output, venue.ofi_time_window);
    output << ',';
    write_optional(output, venue.multi_level_ofi_event_window);
    output << ',';
    write_optional(output, venue.multi_level_ofi_time_window);
    output << ',';
    write_optional(output, venue.signed_trade_volume_event_window);
    output << ',';
    write_optional(output, venue.signed_trade_volume_time_window);
    output << ',';
    write_optional_direction(output, venue.last_mid_move);
}

void write_pair(std::ostream& output, const PairwiseCrossFeatures& pair) {
    output << ',' << (pair.both_fresh ? 1 : 0) << ',';
    write_optional(output, pair.mid_difference);
    output << ',';
    write_optional(output, pair.microprice_difference);
    output << ',';
    write_optional(output, pair.imbalance_l1_difference);
    output << ',';
    write_optional(output, pair.imbalance_l3_difference);
    output << ',';
    write_optional(output, pair.imbalance_l5_difference);
    output << ',';
    write_optional(output, pair.ofi_event_window_difference);
    output << ',';
    write_optional(output, pair.ofi_time_window_difference);
    output << ',';
    write_optional(output, pair.signed_trade_volume_event_window_difference);
    output << ',';
    write_optional(output, pair.signed_trade_volume_time_window_difference);
    output << ',';
    write_optional(output, pair.receipt_timestamp_difference_ns);
    output << ',';
    write_optional(output, pair.exchange_timestamp_difference_ns);
    output << ',';
    write_optional(output, pair.last_mid_move_difference);
}

void write_target(std::ostream& output, const FutureFairValueTarget& target) {
    output << ',';
    write_optional(output, target.target_timestamp_ns);
    output << ',';
    write_optional(output, target.target_delay_ns);
    output << ',';
    write_optional(output, target.future_consolidated_mid);
    output << ',';
    write_optional(output, target.future_consolidated_microprice);
    output << ',';
    write_optional(output, target.mid_return);
    output << ',';
    write_optional(output, target.microprice_return);
    output << ',';
    write_optional_direction(output, target.mid_direction);
    output << ',';
    write_optional_direction(output, target.microprice_direction);
}

void write_header(std::ostream& output, const std::span<const VenueId> venue_ids,
                  const std::span<const VenuePair> pairs,
                  const std::span<const TimestampNs> horizons) {
    output << "sample_kind,sample_timestamp_ns,consolidated_mid,consolidated_microprice,"
              "valid_venue_count,mid_venue_count,microprice_venue_count";
    for (const auto venue_id : venue_ids) {
        write_venue_header(output, venue_id);
    }
    for (const auto pair : pairs) {
        write_pair_header(output, pair);
    }
    for (const auto horizon : horizons) {
        write_target_header(output, horizon);
    }
    output << '\n';
}

void write_sample(std::ostream& output, const CrossVenueSample& sample) {
    output << (sample.sample_kind == SampleKind::Clock ? "clock" : "event") << ','
           << sample.sample_timestamp_ns << ',';
    write_optional(output, sample.consolidated.mid);
    output << ',';
    write_optional(output, sample.consolidated.microprice);
    output << ',' << sample.consolidated.valid_venue_count << ','
           << sample.consolidated.mid_venue_count << ','
           << sample.consolidated.microprice_venue_count;
    for (const auto& venue : sample.venue_features) {
        write_venue(output, venue);
    }
    for (const auto& pair : sample.pairwise_features) {
        write_pair(output, pair);
    }
    for (const auto& target : sample.future_targets) {
        write_target(output, target);
    }
    output << '\n';
}

} // namespace

fairvaluelab::ResearchDatasetReport fairvaluelab::generate_research_dataset_csv(
    std::istream& input, std::ostream& output, ResearchDatasetConfig config) {
    std::string line;
    if (!std::getline(input, line) || !is_normalized_csv_header(line)) {
        throw std::runtime_error("invalid normalized log header");
    }

    std::vector<NormalizedEvent> events;
    std::vector<VenueId> venue_ids;
    std::size_t line_number = 1;
    while (std::getline(input, line)) {
        ++line_number;
        if (line.empty()) {
            continue;
        }
        try {
            auto event = parse_normalized_event(line);
            const auto venue_id = std::visit([](const auto& value) { return value.venue_id; }, event);
            if (std::find(venue_ids.begin(), venue_ids.end(), venue_id) == venue_ids.end()) {
                venue_ids.push_back(venue_id);
            }
            events.push_back(std::move(event));
        } catch (const std::exception& error) {
            throw std::runtime_error("invalid normalized event on line " +
                                     std::to_string(line_number) + ": " + error.what());
        }
    }
    if (venue_ids.empty()) {
        throw std::runtime_error("normalized log contains no events");
    }
    std::sort(venue_ids.begin(), venue_ids.end());

    std::vector<VenuePair> pairs;
    pairs.reserve(venue_ids.size() * (venue_ids.size() - 1) / 2);
    for (std::size_t first = 0; first < venue_ids.size(); ++first) {
        for (std::size_t second = first + 1; second < venue_ids.size(); ++second) {
            pairs.push_back(VenuePair{venue_ids[first], venue_ids[second]});
        }
    }

    ResearchSampler sampler{venue_ids, pairs, config.sampler};
    std::vector<CrossVenueSample> samples;
    samples.reserve(events.size());
    for (const auto& event : events) {
        std::visit(
            [&sampler, &samples](const auto& value) {
                if constexpr (std::is_same_v<std::decay_t<decltype(value)>, BookUpdate>) {
                    static_cast<void>(sampler.process(value, samples));
                } else {
                    sampler.process(value, samples);
                }
            },
            event);
    }
    align_future_targets(samples, config.horizons_ns, config.target_alignment);
    if (!validate_temporal_invariants(samples)) {
        throw std::runtime_error("generated dataset violates temporal invariants");
    }

    output << std::setprecision(17);
    write_header(output, venue_ids, pairs, config.horizons_ns);
    for (const auto& sample : samples) {
        write_sample(output, sample);
    }
    if (!output) {
        throw std::runtime_error("failed to write research dataset");
    }
    return ResearchDatasetReport{
        .input_events = static_cast<std::uint64_t>(events.size()),
        .sample_rows = static_cast<std::uint64_t>(samples.size()),
        .venue_count = venue_ids.size(),
        .pair_count = pairs.size(),
    };
}
