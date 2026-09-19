#include "fairvaluelab/venue.hpp"
#include "fairvaluelab/venue_adapters.hpp"

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

using fairvaluelab::BookUpdate;
using fairvaluelab::AdapterStatus;
using fairvaluelab::KrakenAdapter;
using fairvaluelab::price_to_ticks;
using fairvaluelab::Quantity;
using fairvaluelab::Rational;
using fairvaluelab::scale_quantity;
using fairvaluelab::VenueConfig;

#define FVL_CHECK(condition)                                                                       \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            std::cerr << __FILE__ << ':' << __LINE__ << ": check failed: " #condition << '\n';     \
            return false;                                                                          \
        }                                                                                          \
    } while (false)

bool test_venue_config() {
    const VenueConfig config{7, "example", Rational{1, 100}, 1'000, 32};
    FVL_CHECK(config.venue_id == 7);
    FVL_CHECK(config.name == "example");
    FVL_CHECK(config.tick_size.numerator == 1);
    FVL_CHECK(config.tick_size.denominator == 100);
    FVL_CHECK(config.quantity_scale_factor == 1'000);
    FVL_CHECK(config.max_book_depth == 32);
    return true;
}

bool test_event_defaults() {
    const BookUpdate update{.exchange_timestamp_ns = 42};
    FVL_CHECK(update.venue_id == 0);
    FVL_CHECK(update.local_receipt_timestamp_ns == update.exchange_timestamp_ns);
    return true;
}

bool test_exact_tick_conversion() {
    FVL_CHECK(price_to_ticks(Rational{12'345, 100}, Rational{1, 100}) == 12'345);
    FVL_CHECK(price_to_ticks(Rational{15, 2}, Rational{5, 2}) == 3);
    FVL_CHECK(price_to_ticks(Rational{0, 1}, Rational{1, 100}) == 0);
    return true;
}

bool test_negative_tick_conversion() {
    FVL_CHECK(price_to_ticks(Rational{-12'345, 100}, Rational{1, 100}) == -12'345);
    FVL_CHECK(price_to_ticks(Rational{-15, 2}, Rational{5, 2}) == -3);
    FVL_CHECK(price_to_ticks(Rational{std::numeric_limits<std::int64_t>::min(), 1},
                             Rational{1, 1}) == std::numeric_limits<std::int64_t>::min());
    return true;
}

bool test_non_integral_and_invalid_tick_conversion() {
    FVL_CHECK(!price_to_ticks(Rational{1'001, 100}, Rational{1, 10}).has_value());
    FVL_CHECK(!price_to_ticks(Rational{1, 0}, Rational{1, 1}).has_value());
    FVL_CHECK(!price_to_ticks(Rational{1, 1}, Rational{0, 1}).has_value());
    FVL_CHECK(!price_to_ticks(Rational{1, 1}, Rational{-1, 1}).has_value());
    FVL_CHECK(!price_to_ticks(Rational{1, 1}, Rational{1, 0}).has_value());
    return true;
}

bool test_tick_conversion_limits() {
    constexpr auto maximum = std::numeric_limits<std::int64_t>::max();
    FVL_CHECK(price_to_ticks(Rational{maximum, 1}, Rational{1, 1}) == maximum);
    FVL_CHECK(!price_to_ticks(Rational{maximum, 1}, Rational{1, 2}).has_value());
    FVL_CHECK(price_to_ticks(Rational{maximum, std::numeric_limits<std::uint64_t>::max()},
                             Rational{maximum, std::numeric_limits<std::uint64_t>::max()}) == 1);
    return true;
}

bool test_quantity_scaling_limits() {
    constexpr auto maximum = std::numeric_limits<Quantity>::max();
    FVL_CHECK(scale_quantity(maximum, 1) == maximum);
    FVL_CHECK(scale_quantity(maximum / 10, 10) == (maximum / 10) * 10);
    FVL_CHECK(!scale_quantity(maximum / 10 + 1, 10).has_value());
    FVL_CHECK(scale_quantity(0, maximum) == 0);
    FVL_CHECK(!scale_quantity(1, 0).has_value());
    return true;
}

bool test_kraken_numeric_quantities_are_exact() {
    std::ifstream input{FVL_KRAKEN_NUMERIC_FIXTURE_PATH};
    std::string line;
    FVL_CHECK(static_cast<bool>(std::getline(input, line)));

    KrakenAdapter adapter{VenueConfig{2, "kraken", Rational{1, 10}, 100'000'000, 10}};
    std::vector<BookUpdate> updates;
    FVL_CHECK(adapter.normalize(line, updates) == AdapterStatus::Accepted);
    FVL_CHECK(updates.size() == 2);
    FVL_CHECK(updates[0].quantity == 4'145'246);
    FVL_CHECK(updates[1].quantity == 3'439'124);
    return true;
}

struct TestCase {
    const char* name;
    bool (*run)();
};

int main() {
    constexpr TestCase tests[]{
        {"venue config", test_venue_config},
        {"event defaults", test_event_defaults},
        {"exact tick conversion", test_exact_tick_conversion},
        {"negative tick conversion", test_negative_tick_conversion},
        {"non-integral and invalid tick conversion", test_non_integral_and_invalid_tick_conversion},
        {"tick conversion limits", test_tick_conversion_limits},
        {"quantity scaling limits", test_quantity_scaling_limits},
        {"kraken numeric quantities are exact", test_kraken_numeric_quantities_are_exact},
    };

    std::size_t passed = 0;
    for (const auto& test : tests) {
        if (!test.run()) {
            std::cerr << "FAILED: " << test.name << '\n';
            return 1;
        }
        ++passed;
        std::cout << "PASSED: " << test.name << '\n';
    }
    std::cout << passed << " tests passed\n";
    return 0;
}
