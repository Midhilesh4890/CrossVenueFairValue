#include "fairvaluelab/cross_venue.hpp"
#include "fairvaluelab/feature_emitter.hpp"

#include <array>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace {

std::size_t parse_event_count(const std::string_view value) {
    std::uint64_t parsed{};
    const auto [end, error] =
        std::from_chars(value.data(), value.data() + value.size(), parsed);
    if (error != std::errc{} || end != value.data() + value.size() || parsed == 0 ||
        parsed > std::numeric_limits<std::size_t>::max()) {
        throw std::invalid_argument("invalid event count");
    }
    return static_cast<std::size_t>(parsed);
}

std::vector<fairvaluelab::BookUpdate> make_events(const std::size_t event_count) {
    using namespace fairvaluelab;
    if (event_count > std::numeric_limits<TimestampNs>::max() - 100) {
        throw std::invalid_argument("event count exceeds timestamp range");
    }
    constexpr std::array venues{VenueId{1}, VenueId{2}, VenueId{3}};
    std::array<SequenceNumber, 3> sequences{};
    std::vector<BookUpdate> events;
    events.reserve(event_count);
    for (std::size_t index = 0; index < event_count; ++index) {
        const auto venue_index = index % venues.size();
        const auto sequence = ++sequences[venue_index];
        const auto side = sequence % 2 == 1 ? Side::Bid : Side::Ask;
        const auto price = static_cast<PriceTicks>(10'000 + venue_index * 2 +
                                                   (side == Side::Ask ? 2 : 0));
        const auto timestamp = static_cast<TimestampNs>(100 + index);
        events.push_back(BookUpdate{
            venues[venue_index], side, price, static_cast<Quantity>(10 + index % 100),
            timestamp - 1, timestamp, sequence});
    }
    return events;
}

} // namespace

int main(const int argc, const char* const argv[]) {
    std::size_t event_count = 1'000'000;
    try {
        if (argc == 3 && std::string_view{argv[1]} == "--events") {
            event_count = parse_event_count(argv[2]);
        } else if (argc != 1) {
            throw std::invalid_argument("usage: fvl_cross_venue_benchmark [--events N]");
        }
        const auto events = make_events(event_count);
        constexpr std::array venues{fairvaluelab::VenueId{1}, fairvaluelab::VenueId{2},
                                    fairvaluelab::VenueId{3}};
        constexpr std::array pairs{fairvaluelab::VenuePair{1, 2},
                                   fairvaluelab::VenuePair{1, 3},
                                   fairvaluelab::VenuePair{2, 3}};
        fairvaluelab::FeatureEmitter emitter{fairvaluelab::FeatureEmitterConfig{
            .clock_interval_ns = 1'000,
            .event_window = 100,
            .time_window_ns = 10'000,
            .band_ticks = 5,
            .venue_capacity = 3,
        }};
        fairvaluelab::CrossVenueSynchronizer synchronizer{
            venues, pairs,
            fairvaluelab::CrossVenueSynchronizerConfig{.max_staleness_ns = 10'000}};
        std::vector<fairvaluelab::FeatureSet> emitted;
        emitted.reserve(8);
        std::array<fairvaluelab::VenueCrossFeatures, 3> venue_features{};
        std::array<fairvaluelab::PairwiseCrossFeatures, 3> pairwise_features{};
        std::uint64_t accepted = 0;
        std::uint64_t synchronized_snapshots = 0;
        double checksum = 0.0;

        const auto start = std::chrono::steady_clock::now();
        for (const auto& event : events) {
            emitted.clear();
            accepted += emitter.process(event, emitted).accepted();
            for (const auto& features : emitted) {
                if (synchronizer.update(features) !=
                    fairvaluelab::SynchronizerUpdateStatus::Accepted) {
                    throw std::runtime_error("synchronization failed");
                }
                if (!synchronizer.venue_features(features.sample_timestamp_ns, venue_features) ||
                    !synchronizer.pairwise_features(features.sample_timestamp_ns,
                                                    pairwise_features)) {
                    throw std::runtime_error("cross-venue feature generation failed");
                }
                const auto reference =
                    synchronizer.consolidated_reference(features.sample_timestamp_ns);
                checksum += reference.mid.value_or(0.0);
                checksum += pairwise_features[0].mid_difference.value_or(0.0);
                ++synchronized_snapshots;
            }
        }
        const auto stop = std::chrono::steady_clock::now();
        const auto elapsed_ns =
            std::chrono::duration_cast<std::chrono::nanoseconds>(stop - start).count();
        if (elapsed_ns <= 0) {
            throw std::runtime_error("benchmark timer resolution is insufficient");
        }
        const auto elapsed_seconds = static_cast<double>(elapsed_ns) / 1'000'000'000.0;
        const auto events_per_second = static_cast<double>(event_count) / elapsed_seconds;
        const auto average_ns = static_cast<double>(elapsed_ns) / static_cast<double>(event_count);

        std::cout << std::fixed << std::setprecision(2)
                  << "events processed: " << event_count << '\n'
                  << "accepted events: " << accepted << '\n'
                  << "synchronized snapshots: " << synchronized_snapshots << '\n'
                  << "elapsed seconds: " << elapsed_seconds << '\n'
                  << "events/second: " << events_per_second << '\n'
                  << "average ns/event: " << average_ns << '\n'
                  << "checksum: " << checksum << '\n';
    } catch (const std::exception& error) {
        std::cerr << "cross-venue benchmark failed: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
