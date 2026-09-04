#include "fairvaluelab/order_book.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>

namespace {

using fairvaluelab::ApplyResult;
using fairvaluelab::BookUpdate;
using fairvaluelab::LevelChange;
using fairvaluelab::OrderBook;
using fairvaluelab::PriceLevel;
using fairvaluelab::Side;
using fairvaluelab::UpdateStatus;

#define FVL_CHECK(condition)                                                                       \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            std::cerr << __FILE__ << ':' << __LINE__ << ": check failed: " #condition << '\n';     \
            return false;                                                                          \
        }                                                                                          \
    } while (false)

BookUpdate update(const std::uint64_t sequence, const Side side, const std::int64_t price,
                  const std::uint64_t quantity) {
    return BookUpdate{0, side, price, quantity, sequence * 100, sequence * 100 + 25, sequence};
}

bool approximately_equal(const double lhs, const double rhs) { return std::abs(lhs - rhs) < 1e-12; }

bool test_empty_book() {
    const OrderBook book;
    FVL_CHECK(!book.best_bid().has_value());
    FVL_CHECK(!book.best_ask().has_value());
    FVL_CHECK(!book.spread().has_value());
    FVL_CHECK(!book.mid_price().has_value());
    FVL_CHECK(!book.microprice().has_value());
    FVL_CHECK(!book.depth_imbalance(1).has_value());
    FVL_CHECK(!book.last_sequence_number().has_value());
    FVL_CHECK(book.bids().empty());
    FVL_CHECK(book.asks().empty());
    return true;
}

bool test_insertions_and_ordering() {
    OrderBook book;
    FVL_CHECK(book.apply(update(1, Side::Bid, 100, 10)).change == LevelChange::Inserted);
    FVL_CHECK(book.apply(update(2, Side::Ask, 103, 30)).change == LevelChange::Inserted);
    FVL_CHECK(book.apply(update(3, Side::Bid, 98, 8)).accepted());
    FVL_CHECK(book.apply(update(4, Side::Ask, 105, 50)).accepted());
    FVL_CHECK(book.apply(update(5, Side::Bid, 101, 11)).accepted());
    FVL_CHECK(book.apply(update(6, Side::Ask, 102, 20)).accepted());

    FVL_CHECK((book.best_bid() == PriceLevel{101, 11}));
    FVL_CHECK((book.best_ask() == PriceLevel{102, 20}));
    FVL_CHECK(book.bids().size() == 3);
    FVL_CHECK(book.bids()[0].price_ticks == 101);
    FVL_CHECK(book.bids()[1].price_ticks == 100);
    FVL_CHECK(book.bids()[2].price_ticks == 98);
    FVL_CHECK(book.asks().size() == 3);
    FVL_CHECK(book.asks()[0].price_ticks == 102);
    FVL_CHECK(book.asks()[1].price_ticks == 103);
    FVL_CHECK(book.asks()[2].price_ticks == 105);
    return true;
}

bool test_updates_and_deletions() {
    OrderBook book;
    FVL_CHECK(book.apply(update(1, Side::Bid, 100, 10)).accepted());
    FVL_CHECK(book.apply(update(2, Side::Bid, 99, 9)).accepted());
    FVL_CHECK(book.apply(update(3, Side::Bid, 98, 8)).accepted());

    const ApplyResult changed = book.apply(update(4, Side::Bid, 99, 90));
    FVL_CHECK(changed.change == LevelChange::Updated);
    FVL_CHECK((book.bids()[1] == PriceLevel{99, 90}));

    const ApplyResult removed = book.apply(update(5, Side::Bid, 99, 0));
    FVL_CHECK(removed.change == LevelChange::Removed);
    FVL_CHECK(book.bids().size() == 2);
    FVL_CHECK(book.bids()[0].price_ticks == 100);
    FVL_CHECK(book.bids()[1].price_ticks == 98);

    const ApplyResult absent = book.apply(update(6, Side::Bid, 97, 0));
    FVL_CHECK(absent.change == LevelChange::NotFound);
    FVL_CHECK(absent.accepted());
    FVL_CHECK(book.bids().size() == 2);

    FVL_CHECK(book.apply(update(7, Side::Bid, 100, 0)).change == LevelChange::Removed);
    FVL_CHECK((book.best_bid() == PriceLevel{98, 8}));
    return true;
}

bool test_maximum_depth() {
    OrderBook book{3};
    FVL_CHECK(book.capacity_per_side() == 3);
    FVL_CHECK(book.apply(update(1, Side::Bid, 100, 10)).accepted());
    FVL_CHECK(book.apply(update(2, Side::Bid, 99, 9)).accepted());
    FVL_CHECK(book.apply(update(3, Side::Bid, 98, 8)).accepted());

    const ApplyResult discarded = book.apply(update(4, Side::Bid, 97, 7));
    FVL_CHECK(discarded.accepted());
    FVL_CHECK(discarded.change == LevelChange::Discarded);
    FVL_CHECK(book.bids().size() == 3);
    FVL_CHECK(book.bids().back().price_ticks == 98);

    const ApplyResult inserted = book.apply(update(5, Side::Bid, 101, 11));
    FVL_CHECK(inserted.change == LevelChange::Inserted);
    FVL_CHECK(book.bids().size() == 3);
    FVL_CHECK(book.bids()[0].price_ticks == 101);
    FVL_CHECK(book.bids()[1].price_ticks == 100);
    FVL_CHECK(book.bids()[2].price_ticks == 99);

    FVL_CHECK(book.apply(update(6, Side::Ask, 103, 30)).accepted());
    FVL_CHECK(book.apply(update(7, Side::Ask, 104, 40)).accepted());
    FVL_CHECK(book.apply(update(8, Side::Ask, 105, 50)).accepted());
    FVL_CHECK(book.apply(update(9, Side::Ask, 106, 60)).change == LevelChange::Discarded);
    FVL_CHECK(book.apply(update(10, Side::Ask, 102, 20)).change == LevelChange::Inserted);
    FVL_CHECK(book.asks()[0].price_ticks == 102);
    FVL_CHECK(book.asks()[2].price_ticks == 104);
    return true;
}

bool test_derived_values() {
    OrderBook book;
    FVL_CHECK(book.apply(update(1, Side::Bid, 100, 30)).accepted());
    FVL_CHECK(book.apply(update(2, Side::Bid, 99, 20)).accepted());
    FVL_CHECK(book.apply(update(3, Side::Ask, 104, 10)).accepted());
    FVL_CHECK(book.apply(update(4, Side::Ask, 105, 40)).accepted());

    FVL_CHECK(book.spread() == 4);
    FVL_CHECK(book.mid_price().has_value());
    FVL_CHECK(approximately_equal(*book.mid_price(), 102.0));
    FVL_CHECK(book.microprice().has_value());
    FVL_CHECK(approximately_equal(*book.microprice(), 103.0));
    FVL_CHECK(book.depth_imbalance(1).has_value());
    FVL_CHECK(approximately_equal(*book.depth_imbalance(1), 0.5));
    FVL_CHECK(book.depth_imbalance(2).has_value());
    FVL_CHECK(approximately_equal(*book.depth_imbalance(2), 0.0));
    FVL_CHECK(!book.depth_imbalance(0).has_value());
    return true;
}

bool test_snapshot_and_reset() {
    OrderBook book{3};
    FVL_CHECK(book.apply(update(1, Side::Bid, 1, 1)).accepted());

    constexpr std::array bids{PriceLevel{99, 9}, PriceLevel{101, 11}, PriceLevel{100, 10},
                              PriceLevel{98, 8}};
    constexpr std::array asks{PriceLevel{105, 50}, PriceLevel{103, 30}, PriceLevel{104, 40},
                              PriceLevel{106, 60}};
    book.apply_snapshot(bids, asks, 50);

    FVL_CHECK(book.last_sequence_number() == 50);
    FVL_CHECK(book.bids().size() == 3);
    FVL_CHECK(book.bids()[0].price_ticks == 101);
    FVL_CHECK(book.bids()[2].price_ticks == 99);
    FVL_CHECK(book.asks().size() == 3);
    FVL_CHECK(book.asks()[0].price_ticks == 103);
    FVL_CHECK(book.asks()[2].price_ticks == 105);

    book.reset();
    FVL_CHECK(book.bids().empty());
    FVL_CHECK(book.asks().empty());
    FVL_CHECK(!book.last_sequence_number().has_value());
    FVL_CHECK(book.apply(update(7, Side::Ask, 200, 2)).accepted());
    return true;
}

bool test_sequence_handling() {
    OrderBook book;
    FVL_CHECK(book.apply(update(10, Side::Bid, 100, 10)).status == UpdateStatus::Accepted);
    FVL_CHECK(book.apply(update(11, Side::Ask, 101, 20)).status == UpdateStatus::Accepted);

    FVL_CHECK(book.apply(update(11, Side::Ask, 101, 99)).status == UpdateStatus::Duplicate);
    FVL_CHECK((book.best_ask() == PriceLevel{101, 20}));
    FVL_CHECK(book.apply(update(9, Side::Bid, 100, 99)).status == UpdateStatus::Stale);
    FVL_CHECK((book.best_bid() == PriceLevel{100, 10}));

    FVL_CHECK(book.apply(update(13, Side::Bid, 102, 30)).status == UpdateStatus::SequenceGap);
    FVL_CHECK(book.last_sequence_number() == 11);
    FVL_CHECK((book.best_bid() == PriceLevel{100, 10}));
    FVL_CHECK(book.apply(update(12, Side::Bid, 102, 30)).status == UpdateStatus::Accepted);
    FVL_CHECK((book.best_bid() == PriceLevel{102, 30}));
    return true;
}

bool test_edge_cases() {
    bool rejected_zero_depth = false;
    try {
        static_cast<void>(OrderBook{0});
    } catch (const std::invalid_argument&) {
        rejected_zero_depth = true;
    }
    FVL_CHECK(rejected_zero_depth);

    OrderBook book;
    const auto invalid_side = static_cast<Side>(255);
    FVL_CHECK(book.apply(update(1, invalid_side, 100, 10)).status == UpdateStatus::InvalidSide);
    FVL_CHECK(!book.last_sequence_number().has_value());

    FVL_CHECK(
        book.apply(update(1, Side::Bid, std::numeric_limits<std::int64_t>::min(), 1)).accepted());
    FVL_CHECK(
        book.apply(update(2, Side::Ask, std::numeric_limits<std::int64_t>::max(), 1)).accepted());
    FVL_CHECK(!book.spread().has_value());
    FVL_CHECK(book.mid_price().has_value());
    FVL_CHECK(approximately_equal(*book.mid_price(), -0.5));

    OrderBook maximum_sequence_book;
    FVL_CHECK(maximum_sequence_book
                  .apply(update(std::numeric_limits<std::uint64_t>::max(), Side::Bid, 1, 1))
                  .accepted());
    FVL_CHECK(maximum_sequence_book.apply(update(0, Side::Ask, 2, 1)).status ==
              UpdateStatus::Stale);
    return true;
}

struct TestCase {
    std::string_view name;
    bool (*run)();
};

}

int main() {
    constexpr std::array tests{
        TestCase{"empty book", test_empty_book},
        TestCase{"insertions and ordering", test_insertions_and_ordering},
        TestCase{"updates and deletions", test_updates_and_deletions},
        TestCase{"maximum depth", test_maximum_depth},
        TestCase{"derived values", test_derived_values},
        TestCase{"snapshot and reset", test_snapshot_and_reset},
        TestCase{"sequence handling", test_sequence_handling},
        TestCase{"edge cases", test_edge_cases},
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
