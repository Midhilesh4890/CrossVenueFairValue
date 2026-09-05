#include "fairvaluelab/feature_emitter.hpp"
#include "fairvaluelab/order_book.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string_view>

using fairvaluelab::BookUpdate;
using fairvaluelab::FeatureEmitter;
using fairvaluelab::FeatureEmitterConfig;
using fairvaluelab::OrderBook;
using fairvaluelab::SampleKind;
using fairvaluelab::Side;
using fairvaluelab::Trade;
using fairvaluelab::TradeSide;

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

bool approximately_equal(const double lhs, const double rhs) {
    return std::abs(lhs - rhs) < 1e-12;
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

double latest_ofi(FeatureEmitter& emitter, const BookUpdate& book_update) {
    return emitter.process(book_update).back().ofi_event_window;
}

FeatureEmitter flow_emitter() {
    return FeatureEmitter{FeatureEmitterConfig{
        .clock_interval_ns = 1'000,
        .event_window = 1,
        .time_window_ns = 10'000,
        .band_ticks = 3,
    }};
}

bool test_ofi_sign_convention() {
    auto bid_growth = flow_emitter();
    static_cast<void>(bid_growth.process(update(1, Side::Bid, 100, 10, 100)));
    static_cast<void>(bid_growth.process(update(2, Side::Ask, 102, 10, 200)));
    FVL_CHECK(latest_ofi(bid_growth, update(3, Side::Bid, 100, 15, 300)) == 5.0);

    auto ask_growth = flow_emitter();
    static_cast<void>(ask_growth.process(update(1, Side::Bid, 100, 10, 100)));
    static_cast<void>(ask_growth.process(update(2, Side::Ask, 102, 10, 200)));
    FVL_CHECK(latest_ofi(ask_growth, update(3, Side::Ask, 102, 15, 300)) == -5.0);

    auto bid_improvement = flow_emitter();
    static_cast<void>(bid_improvement.process(update(1, Side::Bid, 100, 10, 100)));
    static_cast<void>(bid_improvement.process(update(2, Side::Ask, 102, 10, 200)));
    FVL_CHECK(latest_ofi(bid_improvement, update(3, Side::Bid, 101, 7, 300)) == 7.0);

    auto ask_improvement = flow_emitter();
    static_cast<void>(ask_improvement.process(update(1, Side::Bid, 100, 10, 100)));
    static_cast<void>(ask_improvement.process(update(2, Side::Ask, 102, 10, 200)));
    FVL_CHECK(latest_ofi(ask_improvement, update(3, Side::Ask, 101, 7, 300)) == -7.0);

    auto bid_deletion = flow_emitter();
    static_cast<void>(bid_deletion.process(update(1, Side::Bid, 100, 10, 100)));
    static_cast<void>(bid_deletion.process(update(2, Side::Ask, 102, 10, 200)));
    FVL_CHECK(latest_ofi(bid_deletion, update(3, Side::Bid, 100, 0, 300)) == -10.0);

    auto ask_deletion = flow_emitter();
    static_cast<void>(ask_deletion.process(update(1, Side::Bid, 100, 10, 100)));
    static_cast<void>(ask_deletion.process(update(2, Side::Ask, 102, 10, 200)));
    FVL_CHECK(latest_ofi(ask_deletion, update(3, Side::Ask, 102, 0, 300)) == 10.0);
    return true;
}

bool test_trade_windows() {
    FeatureEmitter emitter{FeatureEmitterConfig{
        .clock_interval_ns = 1'000,
        .event_window = 2,
        .time_window_ns = 150,
        .band_ticks = 3,
    }};
    static_cast<void>(emitter.process(update(1, Side::Bid, 100, 10, 100)));
    static_cast<void>(emitter.process(update(2, Side::Ask, 102, 10, 200)));
    const Trade buy{7, TradeSide::Buy, 103, 4, 290, 300, 1};
    const Trade sell{7, TradeSide::Sell, 99, 2, 490, 500, 2};
    static_cast<void>(emitter.process(buy));
    const auto features = emitter.process(sell).back();
    FVL_CHECK(features.signed_trade_volume_event_window == 2.0);
    FVL_CHECK(features.signed_trade_volume_time_window == -2.0);
    FVL_CHECK(features.trade_count_event_window == 2);
    FVL_CHECK(features.trade_count_time_window == 1);
    FVL_CHECK(features.trade_vwap_deviation_event_window.has_value());
    FVL_CHECK(approximately_equal(*features.trade_vwap_deviation_event_window, 2.0 / 3.0));
    FVL_CHECK(features.trade_vwap_deviation_time_window == -2.0);
    return true;
}

bool test_trade_csv() {
    std::istringstream input{
        "event_type,sequence_number,venue_id,exchange_timestamp_ns,"
        "local_receipt_timestamp_ns,side,price_ticks,quantity\n"
        "book,1,7,90,100,B,100,10\n"
        "book,2,7,190,200,A,102,10\n"
        "trade,3,7,240,250,B,103,4\n"};
    std::ostringstream output;
    const auto rows = fairvaluelab::write_feature_csv(
        input, output,
        FeatureEmitterConfig{.clock_interval_ns = 1'000,
                             .event_window = 10,
                             .time_window_ns = 1'000,
                             .band_ticks = 3});
    FVL_CHECK(rows == 3);
    FVL_CHECK(output.str().find("event,7,240,250,250,2,101") != std::string::npos);
    FVL_CHECK(output.str().find(",4,4,1,1,2,2\n") != std::string::npos);
    return true;
}

FeatureEmitter band_emitter(const std::uint64_t band_ticks) {
    return FeatureEmitter{FeatureEmitterConfig{
        .clock_interval_ns = 10'000,
        .event_window = 1,
        .time_window_ns = 1,
        .band_ticks = band_ticks,
    }};
}

bool test_band_sign_correctness() {
    for (const auto band : {1, 2, 10}) {
        for (const auto side : {Side::Bid, Side::Ask}) {
            for (const auto quantity : {0, 20}) {
                auto emitter = band_emitter(band);
                static_cast<void>(emitter.process(update(1, Side::Bid, 100, 10, 100)));
                static_cast<void>(emitter.process(update(2, Side::Ask, 102, 10, 110)));
                const auto result = emitter.process(update(3, side,
                    side == Side::Bid ? 100 : 102, quantity, 120)).back();
                FVL_CHECK(result.multi_level_ofi_event_window.has_value());
                FVL_CHECK(result.multi_level_ofi_time_window.has_value());
                const auto expected = (side == Side::Bid ? 1.0 : -1.0) *
                                      (quantity == 0 ? -5.0 : 5.0);
                FVL_CHECK(*result.multi_level_ofi_event_window * expected > 0.0);
                FVL_CHECK(result.multi_level_ofi_event_window == expected);
                FVL_CHECK(result.multi_level_ofi_time_window == expected);
            }
        }
    }
    return true;
}

bool test_band_width_invariance() {
    for (const auto quantity : {0, 20}) {
        std::optional<double> first;
        for (const auto band : {1, 2, 3, 5, 10}) {
            auto emitter = band_emitter(band);
            static_cast<void>(emitter.process(update(1, Side::Bid, 100, 10, 100)));
            static_cast<void>(emitter.process(update(2, Side::Bid, 99, 20, 110)));
            static_cast<void>(emitter.process(update(3, Side::Bid, 98, 30, 120)));
            static_cast<void>(emitter.process(update(4, Side::Ask, 102, 10, 130)));
            const auto result = emitter.process(update(5, Side::Bid, 100, quantity, 140)).back();
            const auto expected = (static_cast<double>(quantity) - 10.0) / 2.0;
            FVL_CHECK(result.multi_level_ofi_event_window == expected);
            FVL_CHECK(result.multi_level_ofi_time_window == expected);
            if (!first.has_value()) {
                first = result.multi_level_ofi_event_window;
            }
            FVL_CHECK(result.multi_level_ofi_event_window == first);
        }
    }
    return true;
}

bool test_band_exclusion_and_absent_values() {
    for (const auto band : {1, 2, 10}) {
        for (const auto side : {Side::Bid, Side::Ask}) {
            auto emitter = band_emitter(band);
            static_cast<void>(emitter.process(update(1, Side::Bid, 100, 10, 100)));
            static_cast<void>(emitter.process(update(2, Side::Ask, 102, 10, 110)));
            const auto price = side == Side::Bid ? 50 : 150;
            static_cast<void>(emitter.process(update(3, side, price, 10, 120)));
            const auto outside = emitter.process(update(4, side, price, 20, 130)).back();
            FVL_CHECK(outside.multi_level_ofi_event_window == 0.0);
            FVL_CHECK(outside.multi_level_ofi_time_window == 0.0);

            auto one_side = band_emitter(band);
            const auto empty = one_side.process(update(1, side, price, 10, 100)).back();
            FVL_CHECK(!empty.multi_level_ofi_event_window.has_value());
            FVL_CHECK(!empty.multi_level_ofi_time_window.has_value());
            const auto missing = one_side.process(update(2, side, price, 20, 110)).back();
            FVL_CHECK(!missing.multi_level_ofi_event_window.has_value());
            FVL_CHECK(!missing.multi_level_ofi_time_window.has_value());
        }
        auto emitter = band_emitter(band);
        static_cast<void>(emitter.process(update(1, Side::Bid, 50, 10, 100)));
        static_cast<void>(emitter.process(update(2, Side::Ask, 150, 10, 110)));
        const auto empty_band = emitter.process(update(3, Side::Bid, 50, 20, 120)).back();
        FVL_CHECK(!empty_band.multi_level_ofi_event_window.has_value());
        FVL_CHECK(!empty_band.multi_level_ofi_time_window.has_value());
    }
    return true;
}

bool test_snapshot_band_and_rejected_updates() {
    for (const auto band : {1, 2, 10}) {
        auto emitter = band_emitter(band);
        static_cast<void>(emitter.process(update(1, Side::Bid, 100, 10, 100)));
        static_cast<void>(emitter.process(update(2, Side::Bid, 99, 20, 110)));
        static_cast<void>(emitter.process(update(3, Side::Ask, 102, 10, 120)));
        FVL_CHECK(emitter.process(update(3, Side::Bid, 100, 999, 130)).empty());
        FVL_CHECK(emitter.process(update(2, Side::Ask, 102, 999, 140)).empty());
        FVL_CHECK(emitter.process(update(5, Side::Bid, 101, 999, 150)).empty());
        const auto deletion = emitter.process(update(4, Side::Bid, 100, 0, 160)).back();
        FVL_CHECK(deletion.ofi_event_window == -10.0);
        FVL_CHECK(deletion.multi_level_ofi_event_window == -5.0);
        FVL_CHECK(deletion.multi_level_ofi_time_window == -5.0);
        const auto empty = emitter.process(update(5, Side::Bid, 99, 0, 170)).back();
        FVL_CHECK(empty.ofi_event_window == -20.0);
        FVL_CHECK(empty.multi_level_ofi_event_window.has_value());
        if (band < 2) {
            FVL_CHECK(empty.multi_level_ofi_event_window == 0.0);
        } else {
            FVL_CHECK(approximately_equal(*empty.multi_level_ofi_event_window, -20.0 / 3.0));
        }
        const auto refill = emitter.process(update(6, Side::Bid, 98, 7, 180)).back();
        FVL_CHECK(refill.ofi_event_window == 7.0);
        FVL_CHECK(!refill.multi_level_ofi_event_window.has_value());
        FVL_CHECK(!refill.multi_level_ofi_time_window.has_value());
    }
    return true;
}

bool test_band_reference_rounding() {
    const auto minimum = std::numeric_limits<std::int64_t>::min();
    const auto maximum = std::numeric_limits<std::int64_t>::max();
    const std::array<std::array<std::int64_t, 3>, 7> cases{{
        {100, 103, 2}, {-103, -100, 1}, {-1, 2, 2}, {-2, 1, 1},
        {maximum - 3, maximum, 2}, {minimum, minimum + 3, 1}, {minimum, maximum, maximum},
    }};
    for (const auto& values : cases) {
        auto emitter = band_emitter(std::numeric_limits<std::uint64_t>::max());
        static_cast<void>(emitter.process(update(1, Side::Bid, values[0], 10, 100)));
        static_cast<void>(emitter.process(update(2, Side::Ask, values[1], 10, 110)));
        const auto result = emitter.process(update(3, Side::Bid, values[0], 20, 120)).back();
        FVL_CHECK(result.multi_level_ofi_event_window.has_value());
        const auto expected = static_cast<double>(10.0L / (1.0L + values[2]));
        FVL_CHECK(*result.multi_level_ofi_event_window == expected);
    }
    return true;
}

bool test_band_windows_and_csv() {
    FeatureEmitter emitter{FeatureEmitterConfig{
        .clock_interval_ns = 100, .event_window = 2, .time_window_ns = 15, .band_ticks = 1,
    }};
    static_cast<void>(emitter.process(update(1, Side::Bid, 100, 10, 100)));
    static_cast<void>(emitter.process(update(2, Side::Ask, 102, 10, 110)));
    const auto first = emitter.process(update(3, Side::Bid, 100, 20, 120)).back();
    FVL_CHECK(first.multi_level_ofi_event_window == 5.0);
    FVL_CHECK(first.multi_level_ofi_time_window == 5.0);
    const auto second = emitter.process(update(4, Side::Ask, 102, 16, 130)).back();
    FVL_CHECK(second.multi_level_ofi_event_window == 2.0);
    FVL_CHECK(second.multi_level_ofi_time_window == 2.0);
    const auto expired = emitter.process(update(5, Side::Bid, 100, 22, 140)).back();
    FVL_CHECK(expired.multi_level_ofi_event_window == -2.0);
    FVL_CHECK(expired.multi_level_ofi_time_window == -2.0);
    const auto clock = emitter.process(update(5, Side::Bid, 100, 22, 200));
    FVL_CHECK(clock.size() == 1);
    FVL_CHECK(clock.front().sample_kind == SampleKind::Clock);
    FVL_CHECK(clock.front().multi_level_ofi_event_window == -2.0);
    FVL_CHECK(!clock.front().multi_level_ofi_time_window.has_value());

    std::istringstream input{
        "sequence_number,venue_id,exchange_timestamp_ns,local_receipt_timestamp_ns,side,"
        "price_ticks,quantity\n"
        "1,7,90,100,B,100,10\n"
        "2,7,100,110,A,102,10\n"
        "3,7,110,120,B,100,0\n"};
    std::ostringstream output;
    FVL_CHECK(fairvaluelab::write_feature_csv(input, output,
        FeatureEmitterConfig{.clock_interval_ns = 10'000, .event_window = 1,
                             .time_window_ns = 1, .band_ticks = 1}) == 3);
    std::istringstream rows{output.str()};
    std::string line;
    FVL_CHECK(static_cast<bool>(std::getline(rows, line)));
    for (int row = 0; row < 3; ++row) {
        FVL_CHECK(static_cast<bool>(std::getline(rows, line)));
        std::istringstream fields{line};
        std::string field;
        for (int column = 0; column <= 18; ++column) {
            FVL_CHECK(static_cast<bool>(std::getline(fields, field, ',')));
            if (column == 17 || column == 18) {
                if (row < 2) {
                    FVL_CHECK(field.empty());
                } else {
                    FVL_CHECK(field == "-5");
                }
            }
        }
    }
    return true;
}

bool test_ring_overwrites_and_expiry() {
    FeatureEmitter emitter{FeatureEmitterConfig{
        .clock_interval_ns = 1'000, .event_window = 2, .time_window_ns = 15,
        .band_ticks = 1, .venue_capacity = 2,
    }};
    FVL_CHECK(!emitter.dropped_entries(7).has_value());
    static_cast<void>(emitter.process(update(1, Side::Bid, 100, 10, 100)));
    static_cast<void>(emitter.process(update(2, Side::Ask, 102, 10, 110)));
    FVL_CHECK(emitter.dropped_entries(7)->order_flow == 0);
    const auto first = emitter.process(update(3, Side::Bid, 100, 14, 120)).back();
    FVL_CHECK(first.ofi_event_window == -6.0);
    FVL_CHECK(first.ofi_time_window == -6.0);
    FVL_CHECK(first.multi_level_ofi_event_window == 2.0);
    FVL_CHECK(emitter.dropped_entries(7)->order_flow == 1);
    const auto second = emitter.process(update(4, Side::Bid, 100, 20, 130)).back();
    FVL_CHECK(second.ofi_event_window == 10.0);
    FVL_CHECK(second.ofi_time_window == 10.0);
    FVL_CHECK(second.multi_level_ofi_event_window == 5.0);
    FVL_CHECK(emitter.dropped_entries(7)->order_flow == 2);
    for (std::uint64_t sequence = 1; sequence <= 4; ++sequence) {
        const auto timestamp = 130 + sequence * 10;
        const auto trade = emitter.process(Trade{
            7, TradeSide::Buy, 103, sequence, timestamp, timestamp, sequence}).back();
        FVL_CHECK(trade.trade_count_event_window == std::min(sequence, std::uint64_t{2}));
    }
    FVL_CHECK(emitter.dropped_entries(7)->trade_flow == 2);
    const auto latest = emitter.process(update(4, Side::Bid, 100, 20, 175));
    FVL_CHECK(latest.empty());
    FVL_CHECK(emitter.dropped_entries(7)->order_flow == 2);
    static_cast<void>(emitter.process(Trade{9, TradeSide::Sell, 103, 10, 180, 180, 1}));
    FVL_CHECK(emitter.dropped_entries(9)->order_flow == 0);
    FVL_CHECK(emitter.dropped_entries(9)->trade_flow == 0);
    const auto clock = emitter.process(update(4, Side::Bid, 100, 20, 2'000));
    FVL_CHECK(clock.size() == 4);
    FVL_CHECK(clock.front().venue_id == 7);
    FVL_CHECK(clock.front().ofi_event_window == 10.0);
    FVL_CHECK(clock.front().ofi_time_window == 0.0);
    FVL_CHECK(clock.front().multi_level_ofi_event_window == 5.0);
    FVL_CHECK(!clock.front().multi_level_ofi_time_window.has_value());
    FVL_CHECK(clock.front().signed_trade_volume_event_window == 7.0);
    FVL_CHECK(clock.front().trade_count_event_window == 2);
    FVL_CHECK(clock.front().trade_vwap_deviation_event_window == 2.0);
    FVL_CHECK(clock.front().trade_count_time_window == 0);
    FVL_CHECK(!clock.front().trade_vwap_deviation_time_window.has_value());
    const auto counts = emitter.dropped_entries();
    FVL_CHECK(counts.size() == 2);
    FVL_CHECK(counts[0].venue_id == 7 && counts[0].order_flow == 2 && counts[0].trade_flow == 2);
    FVL_CHECK(counts[1].venue_id == 9 && counts[1].order_flow == 0 && counts[1].trade_flow == 0);
    return true;
}

bool test_output_append_and_clock_order() {
    const FeatureEmitterConfig config{
        .clock_interval_ns = 1'000, .event_window = 2, .time_window_ns = 10'000,
        .band_ticks = 1, .venue_capacity = 2,
    };
    FeatureEmitter returning{config};
    FeatureEmitter appending{config};
    std::vector<fairvaluelab::FeatureSet> output;
    output.reserve(16);
    fairvaluelab::FeatureSet prefix;
    prefix.venue_id = 999;
    prefix.sample_timestamp_ns = 99'999;
    output.push_back(prefix);
    const std::array updates{
        BookUpdate{9, Side::Bid, 100, 10, 100, 100, 1},
        BookUpdate{7, Side::Bid, 100, 10, 110, 110, 1},
        BookUpdate{9, Side::Ask, 102, 10, 2'100, 2'100, 2},
    };
    for (const auto& item : updates) {
        const auto begin = output.size();
        const auto expected = returning.process(item);
        FVL_CHECK(appending.process(item, output).accepted());
        FVL_CHECK(output.size() == begin + expected.size());
        for (std::size_t index = 0; index < expected.size(); ++index) {
            const auto& actual = output[begin + index];
            FVL_CHECK(actual.venue_id == expected[index].venue_id);
            FVL_CHECK(actual.sample_kind == expected[index].sample_kind);
            FVL_CHECK(actual.sample_timestamp_ns == expected[index].sample_timestamp_ns);
            FVL_CHECK(actual.bid_depth == expected[index].bid_depth);
            FVL_CHECK(actual.ask_depth == expected[index].ask_depth);
            FVL_CHECK(actual.ofi_event_window == expected[index].ofi_event_window);
            FVL_CHECK(actual.multi_level_ofi_event_window == expected[index].multi_level_ofi_event_window);
        }
    }
    FVL_CHECK(output.size() == 8);
    FVL_CHECK(output.front().venue_id == 999 && output.front().sample_timestamp_ns == 99'999);
    for (std::size_t index = 0; index < 4; ++index) {
        FVL_CHECK(output[3 + index].sample_kind == SampleKind::Clock);
        FVL_CHECK(output[3 + index].sample_timestamp_ns == 1'000 + (index / 2) * 1'000);
        FVL_CHECK(output[3 + index].venue_id == (index % 2 == 0 ? 7 : 9));
    }
    const Trade trade{9, TradeSide::Buy, 103, 4, 2'200, 2'200, 1};
    const auto begin = output.size();
    const auto expected = returning.process(trade);
    appending.process(trade, output);
    FVL_CHECK(output.size() == begin + expected.size());
    FVL_CHECK(output.back().signed_trade_volume_event_window == expected.back().signed_trade_volume_event_window);
    FVL_CHECK(output.back().trade_vwap_deviation_event_window == expected.back().trade_vwap_deviation_event_window);
    return true;
}

bool test_emitter_capacity_validation() {
    for (const auto capacity : {std::size_t{0}, FeatureEmitter::maximum_event_window + 1}) {
        bool rejected = false;
        try {
            FeatureEmitter emitter{FeatureEmitterConfig{.event_window = capacity}};
        } catch (const std::invalid_argument&) {
            rejected = true;
        }
        FVL_CHECK(rejected);
    }
    bool rejected = false;
    try {
        FeatureEmitter emitter{FeatureEmitterConfig{.venue_capacity = 0}};
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    FVL_CHECK(rejected);
    FeatureEmitter emitter{FeatureEmitterConfig{.venue_capacity = 1}};
    static_cast<void>(emitter.process(update(1, Side::Bid, 100, 10, 100)));
    rejected = false;
    try {
        static_cast<void>(emitter.process(BookUpdate{9, Side::Bid, 100, 10, 110, 110, 1}));
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    FVL_CHECK(rejected);
    FVL_CHECK(!emitter.dropped_entries(9).has_value());
    FVL_CHECK(emitter.process(update(2, Side::Ask, 102, 10, 120)).size() == 1);
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
        TestCase{"ofi sign convention", test_ofi_sign_convention},
        TestCase{"trade windows", test_trade_windows},
        TestCase{"trade csv", test_trade_csv},
        TestCase{"band sign correctness", test_band_sign_correctness},
        TestCase{"band width invariance", test_band_width_invariance},
        TestCase{"band exclusion and absent values", test_band_exclusion_and_absent_values},
        TestCase{"snapshot band and rejected updates", test_snapshot_band_and_rejected_updates},
        TestCase{"band reference rounding", test_band_reference_rounding},
        TestCase{"band windows and csv", test_band_windows_and_csv},
        TestCase{"ring overwrites and expiry", test_ring_overwrites_and_expiry},
        TestCase{"output append and clock order", test_output_append_and_clock_order},
        TestCase{"emitter capacity validation", test_emitter_capacity_validation},
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
