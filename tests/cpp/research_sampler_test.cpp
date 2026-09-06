#include "fairvaluelab/research_sampler.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
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

CrossVenueSample sample(const std::uint64_t timestamp, const std::optional<double> mid,
                        const std::optional<double> microprice) {
    CrossVenueSample value;
    value.sample_timestamp_ns = timestamp;
    value.consolidated.sample_timestamp_ns = timestamp;
    value.consolidated.mid = mid;
    value.consolidated.microprice = microprice;
    value.consolidated.valid_venue_count = mid.has_value() || microprice.has_value() ? 1 : 0;
    return value;
}

bool test_multiple_future_target_horizons() {
    std::vector samples{
        sample(100, 100.0, 100.5),
        sample(110, 102.0, 101.5),
        sample(155, 99.0, 99.5),
    };
    constexpr std::array horizons{10ULL, 50ULL};
    fairvaluelab::align_future_targets(samples, horizons);

    FVL_CHECK(samples[0].future_targets.size() == 2);
    const auto& short_target = samples[0].future_targets[0];
    FVL_CHECK(short_target.horizon_ns == 10);
    FVL_CHECK(short_target.target_timestamp_ns == 110);
    FVL_CHECK(short_target.target_delay_ns == 0);
    FVL_CHECK(short_target.future_consolidated_mid == 102.0);
    FVL_CHECK(short_target.future_consolidated_microprice == 101.5);
    FVL_CHECK(short_target.mid_return == 2.0);
    FVL_CHECK(short_target.microprice_return == 1.0);
    FVL_CHECK(short_target.mid_direction == 1);
    FVL_CHECK(short_target.microprice_direction == 1);

    const auto& long_target = samples[0].future_targets[1];
    FVL_CHECK(long_target.target_timestamp_ns == 155);
    FVL_CHECK(long_target.target_delay_ns == 5);
    FVL_CHECK(long_target.mid_return == -1.0);
    FVL_CHECK(long_target.microprice_return == -1.0);
    FVL_CHECK(long_target.mid_direction == -1);
    FVL_CHECK(long_target.microprice_direction == -1);

    FVL_CHECK(samples[1].future_targets[0].target_timestamp_ns == 155);
    FVL_CHECK(samples[1].future_targets[0].target_delay_ns == 35);
    FVL_CHECK(!samples.back().future_targets[0].target_timestamp_ns.has_value());
    FVL_CHECK(!samples.back().future_targets[0].mid_return.has_value());
    return true;
}

bool test_target_configuration_validation() {
    std::vector samples{sample(100, 100.0, 100.0)};
    for (const auto horizons : {std::array{0ULL, 10ULL}, std::array{10ULL, 10ULL}}) {
        bool rejected = false;
        try {
            fairvaluelab::align_future_targets(samples, horizons);
        } catch (const std::invalid_argument&) {
            rejected = true;
        }
        FVL_CHECK(rejected);
    }
    return true;
}

bool test_first_target_at_or_after_horizon() {
    std::vector samples{
        sample(100, 100.0, 100.0),
        sample(109, 10'000.0, 10'000.0),
        sample(115, 105.0, 105.0),
        sample(130, 110.0, 110.0),
    };
    constexpr std::array horizons{10ULL};
    fairvaluelab::align_future_targets(
        samples, horizons,
        fairvaluelab::TargetAlignmentConfig{
            .max_target_delay_ns = 5,
            .missing_target_policy = fairvaluelab::MissingTargetPolicy::KeepUndefined,
        });
    FVL_CHECK(samples[0].future_targets[0].target_timestamp_ns == 115);
    FVL_CHECK(samples[0].future_targets[0].target_delay_ns == 5);
    FVL_CHECK(samples[0].future_targets[0].future_consolidated_mid == 105.0);
    FVL_CHECK(samples[0].future_targets[0].mid_return == 5.0);
    FVL_CHECK(!samples[1].future_targets[0].target_timestamp_ns.has_value());
    return true;
}

bool test_maximum_target_delay_and_missing_policy() {
    constexpr std::array horizons{10ULL};
    auto kept = std::vector{
        sample(100, 100.0, 100.0),
        sample(115, 105.0, 105.0),
        sample(130, 110.0, 110.0),
    };
    fairvaluelab::align_future_targets(
        kept, horizons,
        fairvaluelab::TargetAlignmentConfig{
            .max_target_delay_ns = 4,
            .missing_target_policy = fairvaluelab::MissingTargetPolicy::KeepUndefined,
        });
    FVL_CHECK(kept.size() == 3);
    FVL_CHECK(!kept[0].future_targets[0].target_timestamp_ns.has_value());

    auto discarded = std::vector{
        sample(100, 100.0, 100.0),
        sample(115, 105.0, 105.0),
        sample(130, 110.0, 110.0),
    };
    fairvaluelab::align_future_targets(
        discarded, horizons,
        fairvaluelab::TargetAlignmentConfig{
            .max_target_delay_ns = 5,
            .missing_target_policy = fairvaluelab::MissingTargetPolicy::DiscardRow,
        });
    FVL_CHECK(discarded.size() == 2);
    FVL_CHECK(discarded[0].sample_timestamp_ns == 100);
    FVL_CHECK(discarded[1].sample_timestamp_ns == 115);
    return true;
}

bool test_target_alignment_overflow_and_invalid_future_state() {
    constexpr auto maximum = std::numeric_limits<std::uint64_t>::max();
    std::vector samples{
        sample(100, 100.0, 100.0),
        sample(111, std::nullopt, std::nullopt),
        sample(114, 104.0, 104.0),
        sample(maximum - 5, 200.0, 200.0),
    };
    constexpr std::array horizons{10ULL};
    fairvaluelab::align_future_targets(samples, horizons);
    FVL_CHECK(samples[0].future_targets[0].target_timestamp_ns == 114);
    FVL_CHECK(samples[0].future_targets[0].target_delay_ns == 4);
    FVL_CHECK(!samples.back().future_targets[0].target_timestamp_ns.has_value());
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
        TestCase{"multiple future target horizons", test_multiple_future_target_horizons},
        TestCase{"target configuration validation", test_target_configuration_validation},
        TestCase{"first target at or after horizon", test_first_target_at_or_after_horizon},
        TestCase{"maximum target delay and missing policy",
                 test_maximum_target_delay_and_missing_policy},
        TestCase{"target alignment overflow and invalid future state",
                 test_target_alignment_overflow_and_invalid_future_state},
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
