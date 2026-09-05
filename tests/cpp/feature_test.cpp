#include "fairvaluelab/feature_emitter.hpp"
#include "fairvaluelab/order_book.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <sstream>
#include <string_view>

using fairvaluelab::BookUpdate;
using fairvaluelab::FeatureEmitter;
using fairvaluelab::OrderBook;
using fairvaluelab::SampleKind;
using fairvaluelab::Side;

#define FVL_CHECK(condition)                                                                       \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            std::cerr << __FILE__ << ':' << __LINE__ << ": check failed: " #condition << '\n';     \
            return false;                                                                          \
        }                                                                                          \
    } while (false)

BookUpdate update(const std::uint64_t sequence, const Side side, const std::int64_t price,
                  const std::uint64_t quantity, const std::uint64_t timestamp) {
    return BookUpdate{7, side, price, quantity, timestamp - 10, timestamp, sequence};
}

bool test_empty_side_imbalance() {
    FeatureEmitter emitter{100};
    const auto features = emitter.process(update(1, Side::Bid, 100, 20, 100));
    FVL_CHECK(features.size() == 1);
    FVL_CHECK(features.front().sample_kind == SampleKind::Event);
    FVL_CHECK(!features.front().imbalance_l1.has_value());
    FVL_CHECK(!features.front().imbalance_l3.has_value());
    FVL_CHECK(!features.front().imbalance_l5.has_value());
    FVL_CHECK(features.front().bid_depth == 20);
    FVL_CHECK(features.front().ask_depth == 0);
    FVL_CHECK(features.front().time_since_last_update_ns == 0);
    return true;
}

bool test_slope_with_fewer_levels() {
    OrderBook book;
    FVL_CHECK(book.apply(update(1, Side::Bid, 100, 20, 100)).accepted());
    FVL_CHECK(book.apply(update(2, Side::Ask, 102, 10, 200)).accepted());
    const auto sparse = fairvaluelab::compute_features(book, 7, 190, 200, 200,
                                                       SampleKind::Event);
    FVL_CHECK(!sparse.book_slope.has_value());
    FVL_CHECK(book.apply(update(3, Side::Bid, 99, 10, 300)).accepted());
    const auto populated = fairvaluelab::compute_features(book, 7, 290, 300, 300,
                                                          SampleKind::Event);
    FVL_CHECK(populated.book_slope.has_value());
    return true;
}

bool test_clock_sampling_without_updates() {
    FeatureEmitter emitter{100};
    FVL_CHECK(emitter.process(update(1, Side::Bid, 100, 20, 100)).size() == 1);
    const auto features = emitter.process(update(2, Side::Ask, 102, 10, 450));
    FVL_CHECK(features.size() == 4);
    for (std::size_t index = 0; index < 3; ++index) {
        FVL_CHECK(features[index].sample_kind == SampleKind::Clock);
        FVL_CHECK(features[index].sample_timestamp_ns == 200 + index * 100);
        FVL_CHECK(features[index].local_receipt_timestamp_ns == 100);
        FVL_CHECK(features[index].time_since_last_update_ns == 100 + index * 100);
        FVL_CHECK(!features[index].mid_price.has_value());
    }
    FVL_CHECK(features.back().sample_kind == SampleKind::Event);
    FVL_CHECK(features.back().sample_timestamp_ns == 450);
    FVL_CHECK(features.back().mid_price == 101.0);
    return true;
}

bool test_feature_csv() {
    std::istringstream input{
        "sequence_number,venue_id,exchange_timestamp_ns,local_receipt_timestamp_ns,side,"
        "price_ticks,quantity\n"
        "1,7,90,100,B,100,20\n"
        "2,7,440,450,A,102,10\n"};
    std::ostringstream output;
    const auto rows = fairvaluelab::write_feature_csv(input, output, 100);
    FVL_CHECK(rows == 5);
    FVL_CHECK(output.str().find("sample_kind,venue_id") == 0);
    FVL_CHECK(output.str().find("clock,7,90,100,200") != std::string::npos);
    FVL_CHECK(output.str().find("event,7,440,450,450") != std::string::npos);
    return true;
}

struct TestCase {
    std::string_view name;
    bool (*run)();
};

int main() {
    constexpr std::array tests{
        TestCase{"empty side imbalance", test_empty_side_imbalance},
        TestCase{"slope with fewer levels", test_slope_with_fewer_levels},
        TestCase{"clock sampling without updates", test_clock_sampling_without_updates},
        TestCase{"feature csv", test_feature_csv},
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
