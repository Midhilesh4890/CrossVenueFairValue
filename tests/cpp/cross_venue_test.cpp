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
