#include "fairvaluelab/research_sampler.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <string_view>
#include <vector>

namespace {

using fairvaluelab::BookUpdate;
using fairvaluelab::CrossVenueSample;
using fairvaluelab::ResearchSampler;
using fairvaluelab::ResearchSamplerConfig;
using fairvaluelab::SampleKind;
using fairvaluelab::Side;
using fairvaluelab::VenueId;
using fairvaluelab::VenuePair;

#define FVL_CHECK(condition)                                                                       \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            std::cerr << __FILE__ << ':' << __LINE__ << ": check failed: " #condition << '\n';     \
            return false;                                                                          \
        }                                                                                          \
    } while (false)

BookUpdate update(const VenueId venue_id, const std::uint64_t sequence, const Side side,
                  const std::int64_t price, const std::uint64_t quantity,
                  const std::uint64_t timestamp) {
    return BookUpdate{venue_id, side, price, quantity, timestamp - 1, timestamp, sequence};
}

ResearchSampler make_sampler(const SampleKind sample_kind, const std::uint64_t clock_interval) {
    static constexpr std::array venues{VenueId{1}, VenueId{2}};
    static constexpr std::array pairs{VenuePair{1, 2}};
    return ResearchSampler{
        venues, pairs,
        ResearchSamplerConfig{
            .sample_kind = sample_kind,
            .feature_emitter = fairvaluelab::FeatureEmitterConfig{
                .clock_interval_ns = clock_interval,
                .event_window = 10,
                .time_window_ns = 1'000,
                .band_ticks = 2,
                .venue_capacity = 2,
            },
            .synchronizer = fairvaluelab::CrossVenueSynchronizerConfig{
                .max_staleness_ns = 1'000},
        }};
}

bool test_clock_sampling_groups_venues() {
    auto sampler = make_sampler(SampleKind::Clock, 100);
    std::vector<CrossVenueSample> output;
    FVL_CHECK(sampler.process(update(1, 1, Side::Bid, 100, 10, 100), output).accepted());
    FVL_CHECK(sampler.process(update(1, 2, Side::Ask, 102, 10, 110), output).accepted());
    FVL_CHECK(sampler.process(update(2, 1, Side::Bid, 200, 20, 120), output).accepted());
    FVL_CHECK(sampler.process(update(2, 2, Side::Ask, 204, 20, 130), output).accepted());
    FVL_CHECK(output.empty());

    FVL_CHECK(sampler.process(update(1, 3, Side::Bid, 100, 12, 350), output).accepted());
    FVL_CHECK(output.size() == 2);
    FVL_CHECK(output[0].sample_kind == SampleKind::Clock);
    FVL_CHECK(output[0].sample_timestamp_ns == 200);
    FVL_CHECK(output[1].sample_timestamp_ns == 300);
    FVL_CHECK(output[0].consolidated.valid_venue_count == 2);
    FVL_CHECK(output[0].consolidated.mid == 151.5);
    FVL_CHECK(output[0].venue_features.size() == 2);
    FVL_CHECK(output[0].pairwise_features.size() == 1);
    FVL_CHECK(output[0].pairwise_features[0].mid_difference == -101.0);
    return true;
}

bool test_event_sampling_uses_only_event_rows() {
    auto sampler = make_sampler(SampleKind::Event, 100);
    std::vector<CrossVenueSample> output;
    FVL_CHECK(sampler.process(update(1, 1, Side::Bid, 100, 10, 100), output).accepted());
    FVL_CHECK(sampler.process(update(1, 2, Side::Ask, 102, 10, 110), output).accepted());
    FVL_CHECK(sampler.process(update(2, 1, Side::Bid, 200, 20, 120), output).accepted());
    FVL_CHECK(sampler.process(update(2, 2, Side::Ask, 204, 20, 130), output).accepted());
    FVL_CHECK(sampler.process(update(1, 3, Side::Bid, 101, 12, 350), output).accepted());
    FVL_CHECK(output.size() == 5);
    constexpr std::array expected_timestamps{100ULL, 110ULL, 120ULL, 130ULL, 350ULL};
    for (std::size_t index = 0; index < output.size(); ++index) {
        FVL_CHECK(output[index].sample_kind == SampleKind::Event);
        FVL_CHECK(output[index].sample_timestamp_ns == expected_timestamps[index]);
    }
    FVL_CHECK(output.back().consolidated.valid_venue_count == 2);
    return true;
}

bool test_configurable_clock_intervals() {
    for (const auto interval : {10ULL, 50ULL, 100ULL}) {
        static constexpr std::array venue{VenueId{1}};
        const std::array<VenuePair, 0> no_pairs{};
        ResearchSampler sampler{
            venue, no_pairs,
            ResearchSamplerConfig{
                .sample_kind = SampleKind::Clock,
                .feature_emitter = fairvaluelab::FeatureEmitterConfig{
                    .clock_interval_ns = interval,
                    .venue_capacity = 1,
                },
                .synchronizer = fairvaluelab::CrossVenueSynchronizerConfig{},
            }};
        std::vector<CrossVenueSample> output;
        FVL_CHECK(sampler.process(update(1, 1, Side::Bid, 100, 10, 100), output).accepted());
        FVL_CHECK(sampler.process(update(1, 2, Side::Ask, 102, 10, 110), output).accepted());
        output.clear();
        const auto final_timestamp = interval == 10 ? 141ULL : 260ULL;
        FVL_CHECK(sampler.process(update(1, 3, Side::Bid, 100, 11, final_timestamp), output)
                      .accepted());
        FVL_CHECK(!output.empty());
        for (std::size_t index = 1; index < output.size(); ++index) {
            FVL_CHECK(output[index].sample_timestamp_ns - output[index - 1].sample_timestamp_ns ==
                      interval);
        }
    }
    return true;
}

struct TestCase {
    std::string_view name;
    bool (*run)();
};

} // namespace

int main() {
    constexpr std::array tests{
        TestCase{"clock sampling groups venues", test_clock_sampling_groups_venues},
        TestCase{"event sampling uses only event rows", test_event_sampling_uses_only_event_rows},
        TestCase{"configurable clock intervals", test_configurable_clock_intervals},
    };
    for (const auto& test : tests) {
        if (!test.run()) {
            std::cerr << "FAILED: " << test.name << '\n';
            return 1;
        }
        std::cout << "PASSED: " << test.name << '\n';
    }
    return 0;
}
