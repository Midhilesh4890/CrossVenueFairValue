#include "fairvaluelab/cross_venue.hpp"

#include <array>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace {

using fairvaluelab::CrossVenueSynchronizer;
using fairvaluelab::CrossVenueSynchronizerConfig;
using fairvaluelab::FeatureSet;
using fairvaluelab::SynchronizerUpdateStatus;
using fairvaluelab::VenuePair;

#define FVL_CHECK(condition)                                                                       \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            std::cerr << __FILE__ << ':' << __LINE__ << ": check failed: " #condition << '\n';     \
            return false;                                                                          \
        }                                                                                          \
    } while (false)

FeatureSet features(const fairvaluelab::VenueId venue_id,
                    const fairvaluelab::TimestampNs receipt_timestamp,
                    const fairvaluelab::TimestampNs sample_timestamp, const double mid) {
    FeatureSet value;
    value.venue_id = venue_id;
    value.exchange_timestamp_ns = receipt_timestamp - 5;
    value.local_receipt_timestamp_ns = receipt_timestamp;
    value.sample_timestamp_ns = sample_timestamp;
    value.spread_ticks = 2;
    value.mid_price = mid;
    value.microprice = mid + 0.25;
    value.imbalance_l1 = 0.25;
    value.best_bid_ticks = static_cast<fairvaluelab::PriceTicks>(mid - 1.0);
    value.best_ask_ticks = static_cast<fairvaluelab::PriceTicks>(mid + 1.0);
    return value;
}

bool test_latest_state_selection() {
    constexpr std::array venues{fairvaluelab::VenueId{10}, fairvaluelab::VenueId{20}};
    CrossVenueSynchronizer synchronizer{venues};
    FVL_CHECK(synchronizer.states().size() == 2);
    FVL_CHECK(!synchronizer.state(10)->get().observed);

    FVL_CHECK(synchronizer.update(features(10, 100, 100, 101.0)) ==
              SynchronizerUpdateStatus::Accepted);
    FVL_CHECK(synchronizer.update(features(20, 105, 105, 102.0)) ==
              SynchronizerUpdateStatus::Accepted);
    FVL_CHECK(synchronizer.update(features(10, 110, 110, 103.0)) ==
              SynchronizerUpdateStatus::Accepted);

    const auto& first = synchronizer.state(10)->get();
    FVL_CHECK(first.observed);
    FVL_CHECK(first.latest_local_receipt_timestamp_ns == 110);
    FVL_CHECK(first.latest_exchange_timestamp_ns == 105);
    FVL_CHECK(first.best_bid_ticks == 102);
    FVL_CHECK(first.best_ask_ticks == 104);
    FVL_CHECK(first.features.mid_price == 103.0);
    FVL_CHECK(synchronizer.state(20)->get().features.mid_price == 102.0);
    FVL_CHECK(synchronizer.latest_sample_timestamp_ns() == 110);
    FVL_CHECK(!synchronizer.state(30).has_value());
    return true;
}

bool test_invalid_updates_do_not_mutate_state() {
    constexpr std::array venues{fairvaluelab::VenueId{10}};
    CrossVenueSynchronizer synchronizer{venues};
    FVL_CHECK(synchronizer.update(features(10, 100, 100, 101.0)) ==
              SynchronizerUpdateStatus::Accepted);
    FVL_CHECK(synchronizer.update(features(10, 99, 101, 999.0)) ==
              SynchronizerUpdateStatus::OutOfOrder);
    FVL_CHECK(synchronizer.update(features(10, 110, 99, 999.0)) ==
              SynchronizerUpdateStatus::InvalidTimestamp);
    FVL_CHECK(synchronizer.update(features(20, 110, 110, 999.0)) ==
              SynchronizerUpdateStatus::UnknownVenue);
    FVL_CHECK(synchronizer.state(10)->get().features.mid_price == 101.0);
    FVL_CHECK(synchronizer.latest_sample_timestamp_ns() == 100);
    return true;
}

bool test_configuration_validation() {
    bool empty_rejected = false;
    try {
        CrossVenueSynchronizer synchronizer{std::span<const fairvaluelab::VenueId>{}};
    } catch (const std::invalid_argument&) {
        empty_rejected = true;
    }
    FVL_CHECK(empty_rejected);

    constexpr std::array duplicate{fairvaluelab::VenueId{10}, fairvaluelab::VenueId{10}};
    bool duplicate_rejected = false;
    try {
        CrossVenueSynchronizer synchronizer{duplicate};
    } catch (const std::invalid_argument&) {
        duplicate_rejected = true;
    }
    FVL_CHECK(duplicate_rejected);
    return true;
}

bool test_venue_freshness_boundaries_and_recovery() {
    constexpr std::array venues{fairvaluelab::VenueId{10}, fairvaluelab::VenueId{20}};
    CrossVenueSynchronizer synchronizer{
        venues, CrossVenueSynchronizerConfig{.max_staleness_ns = 100}};
    FVL_CHECK(synchronizer.max_staleness_ns() == 100);

    const auto never_observed = synchronizer.freshness(20, 1'000);
    FVL_CHECK(!never_observed.observed);
    FVL_CHECK(!never_observed.usable);
    FVL_CHECK(!never_observed.age_ns.has_value());

    FVL_CHECK(synchronizer.update(features(10, 1'000, 1'000, 101.0)) ==
              SynchronizerUpdateStatus::Accepted);
    const auto fresh = synchronizer.freshness(10, 1'050);
    FVL_CHECK(fresh.observed);
    FVL_CHECK(fresh.usable);
    FVL_CHECK(fresh.age_ns == 50);

    const auto threshold = synchronizer.freshness(10, 1'100);
    FVL_CHECK(threshold.usable);
    FVL_CHECK(threshold.age_ns == 100);

    const auto stale = synchronizer.freshness(10, 1'101);
    FVL_CHECK(!stale.usable);
    FVL_CHECK(stale.age_ns == 101);

    const auto invalid_order = synchronizer.freshness(10, 999);
    FVL_CHECK(invalid_order.observed);
    FVL_CHECK(!invalid_order.usable);
    FVL_CHECK(!invalid_order.age_ns.has_value());

    FVL_CHECK(synchronizer.update(features(10, 1'200, 1'200, 102.0)) ==
              SynchronizerUpdateStatus::Accepted);
    const auto recovered = synchronizer.freshness(10, 1'200);
    FVL_CHECK(recovered.usable);
    FVL_CHECK(recovered.age_ns == 0);
    return true;
}

bool test_consolidated_reference() {
    constexpr std::array venues{fairvaluelab::VenueId{10}, fairvaluelab::VenueId{20},
                                fairvaluelab::VenueId{30}};
    CrossVenueSynchronizer synchronizer{
        venues, CrossVenueSynchronizerConfig{.max_staleness_ns = 100}};
    const auto empty = synchronizer.consolidated_reference(1'000);
    FVL_CHECK(!empty.mid.has_value());
    FVL_CHECK(!empty.microprice.has_value());
    FVL_CHECK(empty.valid_venue_count == 0);

    auto first = features(10, 1'000, 1'000, 101.0);
    first.microprice = 101.25;
    FVL_CHECK(synchronizer.update(first) == SynchronizerUpdateStatus::Accepted);
    const auto one = synchronizer.consolidated_reference(1'000);
    FVL_CHECK(one.mid == 101.0);
    FVL_CHECK(one.microprice == 101.25);
    FVL_CHECK(one.valid_venue_count == 1);
    FVL_CHECK(one.mid_venue_count == 1);
    FVL_CHECK(one.microprice_venue_count == 1);

    auto second = features(20, 1'010, 1'010, 103.0);
    second.microprice = 102.75;
    FVL_CHECK(synchronizer.update(second) == SynchronizerUpdateStatus::Accepted);
    const auto two = synchronizer.consolidated_reference(1'050);
    FVL_CHECK(two.mid == 102.0);
    FVL_CHECK(two.microprice == 102.0);
    FVL_CHECK(two.valid_venue_count == 2);

    auto crossed = features(30, 1'020, 1'020, 104.0);
    crossed.best_bid_ticks = 105;
    crossed.best_ask_ticks = 103;
    FVL_CHECK(synchronizer.update(crossed) == SynchronizerUpdateStatus::Accepted);
    const auto without_crossed = synchronizer.consolidated_reference(1'050);
    FVL_CHECK(without_crossed.valid_venue_count == 2);
    FVL_CHECK(without_crossed.mid == 102.0);

    const auto stale = synchronizer.consolidated_reference(1'111);
    FVL_CHECK(stale.valid_venue_count == 0);
    FVL_CHECK(!stale.mid.has_value());
    FVL_CHECK(!stale.microprice.has_value());
    return true;
}

bool test_consolidated_missing_values() {
    constexpr std::array venues{fairvaluelab::VenueId{10}, fairvaluelab::VenueId{20}};
    CrossVenueSynchronizer synchronizer{venues};
    auto first = features(10, 100, 100, 101.0);
    first.microprice.reset();
    auto second = features(20, 100, 100, 103.0);
    FVL_CHECK(synchronizer.update(first) == SynchronizerUpdateStatus::Accepted);
    FVL_CHECK(synchronizer.update(second) == SynchronizerUpdateStatus::Accepted);
    const auto reference = synchronizer.consolidated_reference(100);
    FVL_CHECK(reference.valid_venue_count == 2);
    FVL_CHECK(reference.mid_venue_count == 2);
    FVL_CHECK(reference.mid == 102.0);
    FVL_CHECK(reference.microprice_venue_count == 1);
    FVL_CHECK(reference.microprice == 103.25);
    return true;
}

bool test_per_venue_cross_features() {
    constexpr std::array venues{fairvaluelab::VenueId{10}, fairvaluelab::VenueId{20},
                                fairvaluelab::VenueId{30}};
    CrossVenueSynchronizer synchronizer{
        venues, CrossVenueSynchronizerConfig{.max_staleness_ns = 100}};
    auto first = features(10, 1'000, 1'000, 101.0);
    first.imbalance_l3 = 0.2;
    first.imbalance_l5 = 0.1;
    first.bid_depth = 40;
    first.ask_depth = 30;
    first.ofi_event_window = 8.0;
    first.ofi_time_window = 5.0;
    first.multi_level_ofi_event_window = 4.0;
    first.multi_level_ofi_time_window = 3.0;
    first.signed_trade_volume_event_window = 7.0;
    first.signed_trade_volume_time_window = 2.0;
    auto second = features(20, 1'050, 1'050, 103.0);
    FVL_CHECK(synchronizer.update(first) == SynchronizerUpdateStatus::Accepted);
    FVL_CHECK(synchronizer.update(second) == SynchronizerUpdateStatus::Accepted);

    std::array<fairvaluelab::VenueCrossFeatures, 3> output{};
    FVL_CHECK(synchronizer.venue_count() == output.size());
    FVL_CHECK(synchronizer.venue_features(1'075, output));
    FVL_CHECK(output[0].venue_id == 10);
    FVL_CHECK(output[0].fresh);
    FVL_CHECK(output[0].age_ns == 75);
    FVL_CHECK(output[0].mid_minus_consolidated_mid == -1.0);
    FVL_CHECK(output[0].microprice_minus_consolidated_microprice == -1.0);
    FVL_CHECK(output[0].spread_ticks == 2);
    FVL_CHECK(output[0].imbalance_l1 == 0.25);
    FVL_CHECK(output[0].imbalance_l3 == 0.2);
    FVL_CHECK(output[0].imbalance_l5 == 0.1);
    FVL_CHECK(output[0].bid_depth == 40);
    FVL_CHECK(output[0].ask_depth == 30);
    FVL_CHECK(output[0].ofi_event_window == 8.0);
    FVL_CHECK(output[0].ofi_time_window == 5.0);
    FVL_CHECK(output[0].multi_level_ofi_event_window == 4.0);
    FVL_CHECK(output[0].multi_level_ofi_time_window == 3.0);
    FVL_CHECK(output[0].signed_trade_volume_event_window == 7.0);
    FVL_CHECK(output[0].signed_trade_volume_time_window == 2.0);
    FVL_CHECK(output[1].mid_minus_consolidated_mid == 1.0);
    FVL_CHECK(!output[2].observed);
    FVL_CHECK(!output[2].fresh);
    FVL_CHECK(!output[2].age_ns.has_value());
    FVL_CHECK(!output[2].ofi_event_window.has_value());

    FVL_CHECK(synchronizer.venue_features(1'101, output));
    FVL_CHECK(!output[0].fresh);
    FVL_CHECK(output[0].age_ns == 101);
    FVL_CHECK(!output[0].spread_ticks.has_value());
    FVL_CHECK(!output[0].mid_minus_consolidated_mid.has_value());
    FVL_CHECK(output[1].fresh);

    std::array<fairvaluelab::VenueCrossFeatures, 2> wrong_size{};
    FVL_CHECK(!synchronizer.venue_features(1'101, wrong_size));
    return true;
}

bool test_pairwise_cross_features() {
    constexpr std::array venues{fairvaluelab::VenueId{10}, fairvaluelab::VenueId{20},
                                fairvaluelab::VenueId{30}};
    constexpr std::array pairs{VenuePair{10, 20}, VenuePair{20, 30}};
    CrossVenueSynchronizer synchronizer{
        venues, pairs, CrossVenueSynchronizerConfig{.max_staleness_ns = 100}};
    auto first = features(10, 1'000, 1'000, 101.0);
    first.imbalance_l1 = 0.5;
    first.imbalance_l3 = 0.4;
    first.imbalance_l5 = 0.3;
    first.ofi_event_window = 7.0;
    first.ofi_time_window = 6.0;
    first.signed_trade_volume_event_window = 5.0;
    first.signed_trade_volume_time_window = 4.0;
    auto second = features(20, 1'010, 1'010, 103.0);
    second.imbalance_l1 = -0.5;
    second.imbalance_l3 = -0.4;
    second.imbalance_l5 = -0.3;
    second.ofi_event_window = 2.0;
    second.ofi_time_window = 1.0;
    second.signed_trade_volume_event_window = -1.0;
    second.signed_trade_volume_time_window = -2.0;
    FVL_CHECK(synchronizer.update(first) == SynchronizerUpdateStatus::Accepted);
    FVL_CHECK(synchronizer.update(second) == SynchronizerUpdateStatus::Accepted);

    std::array<fairvaluelab::PairwiseCrossFeatures, 2> output{};
    FVL_CHECK(synchronizer.pair_count() == 2);
    FVL_CHECK(synchronizer.venue_pairs()[0].first == 10);
    FVL_CHECK(synchronizer.venue_pairs()[0].second == 20);
    FVL_CHECK(synchronizer.pairwise_features(1'050, output));
    FVL_CHECK(output[0].both_fresh);
    FVL_CHECK(output[0].mid_difference == -2.0);
    FVL_CHECK(output[0].microprice_difference == -2.0);
    FVL_CHECK(output[0].imbalance_l1_difference == 1.0);
    FVL_CHECK(output[0].imbalance_l3_difference == 0.8);
    FVL_CHECK(output[0].imbalance_l5_difference == 0.6);
    FVL_CHECK(output[0].ofi_event_window_difference == 5.0);
    FVL_CHECK(output[0].ofi_time_window_difference == 5.0);
    FVL_CHECK(output[0].signed_trade_volume_event_window_difference == 6.0);
    FVL_CHECK(output[0].signed_trade_volume_time_window_difference == 6.0);
    FVL_CHECK(output[0].receipt_timestamp_difference_ns == -10);
    FVL_CHECK(!output[1].both_fresh);
    FVL_CHECK(!output[1].mid_difference.has_value());

    second.imbalance_l3.reset();
    second.sample_timestamp_ns = 1'060;
    second.local_receipt_timestamp_ns = 1'060;
    FVL_CHECK(synchronizer.update(second) == SynchronizerUpdateStatus::Accepted);
    FVL_CHECK(synchronizer.pairwise_features(1'060, output));
    FVL_CHECK(!output[0].imbalance_l3_difference.has_value());
    FVL_CHECK(output[0].mid_difference == -2.0);

    std::array<fairvaluelab::PairwiseCrossFeatures, 1> wrong_size{};
    FVL_CHECK(!synchronizer.pairwise_features(1'060, wrong_size));
    return true;
}

bool test_pair_configuration_validation() {
    constexpr std::array venues{fairvaluelab::VenueId{10}, fairvaluelab::VenueId{20}};
    for (const auto invalid : {VenuePair{10, 10}, VenuePair{10, 30}}) {
        bool rejected = false;
        try {
            const std::array pairs{invalid};
            CrossVenueSynchronizer synchronizer{venues, pairs};
        } catch (const std::invalid_argument&) {
            rejected = true;
        }
        FVL_CHECK(rejected);
    }
    constexpr std::array duplicates{VenuePair{10, 20}, VenuePair{10, 20}};
    bool duplicate_rejected = false;
    try {
        CrossVenueSynchronizer synchronizer{venues, duplicates};
    } catch (const std::invalid_argument&) {
        duplicate_rejected = true;
    }
    FVL_CHECK(duplicate_rejected);
    return true;
}

bool test_lead_lag_and_mid_move_features() {
    constexpr std::array venues{fairvaluelab::VenueId{10}, fairvaluelab::VenueId{20}};
    constexpr std::array pairs{VenuePair{10, 20}};
    CrossVenueSynchronizer synchronizer{venues, pairs};
    FVL_CHECK(synchronizer.update(features(10, 1'000, 1'000, 101.0)) ==
              SynchronizerUpdateStatus::Accepted);
    FVL_CHECK(synchronizer.update(features(20, 1'010, 1'010, 102.0)) ==
              SynchronizerUpdateStatus::Accepted);

    std::array<fairvaluelab::PairwiseCrossFeatures, 1> pair_output{};
    std::array<fairvaluelab::VenueCrossFeatures, 2> venue_output{};
    FVL_CHECK(synchronizer.pairwise_features(1'010, pair_output));
    FVL_CHECK(pair_output[0].receipt_timestamp_difference_ns == -10);
    FVL_CHECK(pair_output[0].exchange_timestamp_difference_ns == -10);
    FVL_CHECK(pair_output[0].last_mid_move_difference == 0);

    auto first_up = features(10, 1'020, 1'020, 103.0);
    FVL_CHECK(synchronizer.update(first_up) == SynchronizerUpdateStatus::Accepted);
    FVL_CHECK(synchronizer.venue_features(1'020, venue_output));
    FVL_CHECK(venue_output[0].last_mid_move == 1);
    FVL_CHECK(venue_output[1].last_mid_move == 0);
    FVL_CHECK(synchronizer.pairwise_features(1'020, pair_output));
    FVL_CHECK(pair_output[0].last_mid_move_difference == 1);

    auto clock = first_up;
    clock.sample_kind = fairvaluelab::SampleKind::Clock;
    clock.sample_timestamp_ns = 1'030;
    FVL_CHECK(synchronizer.update(clock) == SynchronizerUpdateStatus::Accepted);
    FVL_CHECK(synchronizer.venue_features(1'030, venue_output));
    FVL_CHECK(venue_output[0].last_mid_move == 1);

    auto second_down = features(20, 1'040, 1'040, 100.0);
    FVL_CHECK(synchronizer.update(second_down) == SynchronizerUpdateStatus::Accepted);
    FVL_CHECK(synchronizer.pairwise_features(1'040, pair_output));
    FVL_CHECK(pair_output[0].last_mid_move_difference == 2);

    auto missing_exchange = second_down;
    missing_exchange.exchange_timestamp_ns = 0;
    missing_exchange.local_receipt_timestamp_ns = 1'050;
    missing_exchange.sample_timestamp_ns = 1'050;
    FVL_CHECK(synchronizer.update(missing_exchange) == SynchronizerUpdateStatus::Accepted);
    FVL_CHECK(synchronizer.pairwise_features(1'050, pair_output));
    FVL_CHECK(!pair_output[0].exchange_timestamp_difference_ns.has_value());
    return true;
}

struct TestCase {
    std::string_view name;
    bool (*run)();
};

} // namespace

int main() {
    constexpr std::array tests{
        TestCase{"latest state selection", test_latest_state_selection},
        TestCase{"invalid updates do not mutate state", test_invalid_updates_do_not_mutate_state},
        TestCase{"configuration validation", test_configuration_validation},
        TestCase{"venue freshness boundaries and recovery",
                 test_venue_freshness_boundaries_and_recovery},
        TestCase{"consolidated reference", test_consolidated_reference},
        TestCase{"consolidated missing values", test_consolidated_missing_values},
        TestCase{"per-venue cross features", test_per_venue_cross_features},
        TestCase{"pairwise cross features", test_pairwise_cross_features},
        TestCase{"pair configuration validation", test_pair_configuration_validation},
        TestCase{"lead-lag and mid-move features", test_lead_lag_and_mid_move_features},
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
