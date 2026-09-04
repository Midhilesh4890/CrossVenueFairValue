#include "fairvaluelab/capture_converter.hpp"
#include "fairvaluelab/multi_replay.hpp"
#include "fairvaluelab/venue.hpp"
#include "fairvaluelab/venue_adapter.hpp"
#include "fairvaluelab/venue_adapters.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#ifndef FVL_MULTI_FIXTURE_PATH
#define FVL_MULTI_FIXTURE_PATH "data/fixtures/multi_venue"
#endif

using fairvaluelab::AdapterStatus;
using fairvaluelab::BinanceAdapter;
using fairvaluelab::BookUpdate;
using fairvaluelab::CoinbaseAdapter;
using fairvaluelab::OkxAdapter;
using fairvaluelab::Rational;
using fairvaluelab::Side;
using fairvaluelab::VenueConfig;

#define FVL_CHECK(condition)                                                                       \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            std::cerr << __FILE__ << ':' << __LINE__ << ": check failed: " #condition << '\n';     \
            return false;                                                                          \
        }                                                                                          \
    } while (false)

bool test_concrete_adapters() {
    const BinanceAdapter binance{VenueConfig{1, "binance", Rational{1, 100}, 100'000'000, 64}};
    const CoinbaseAdapter coinbase{VenueConfig{2, "coinbase", Rational{1, 100}, 100'000'000, 64}};
    const OkxAdapter okx{VenueConfig{3, "okx", Rational{1, 10}, 100'000'000, 64}};

    std::vector<BookUpdate> updates;
    const std::string binance_record =
        R"({"local_receipt_timestamp_ns":1704067200000000100,"raw_payload":"{\"stream\":\"btcusdt@depth@100ms\",\"data\":{\"e\":\"depthUpdate\",\"E\":1704067200000,\"u\":11,\"b\":[[\"50000.25\",\"1.25000000\"],[\"50000.00\",\"1.00000000\"]],\"a\":[[\"50001.00\",\"2.00000000\"]]}}"})";
    FVL_CHECK(binance.normalize(binance_record, updates) == AdapterStatus::Accepted);
    FVL_CHECK(updates.size() == 3);
    const auto update = updates.front();
    FVL_CHECK(update.venue_id == 1);
    FVL_CHECK(update.side == Side::Bid);
    FVL_CHECK(update.price_ticks == 5'000'025);
    FVL_CHECK(update.quantity == 125'000'000);
    FVL_CHECK(update.exchange_timestamp_ns == 1'704'067'200'000'000'000);
    FVL_CHECK(update.local_receipt_timestamp_ns == 1'704'067'200'000'000'100);
    FVL_CHECK(update.sequence_number == 1);
    FVL_CHECK(updates[1].sequence_number == 2);
    FVL_CHECK(updates[2].sequence_number == 3);
    FVL_CHECK(updates[2].side == Side::Ask);

    const std::string coinbase_record =
        R"({"local_receipt_timestamp_ns":1704067200000000200,"raw_payload":"{\"channel\":\"l2_data\",\"sequence_num\":12,\"events\":[{\"updates\":[{\"side\":\"offer\",\"event_time\":\"2024-01-01T00:00:00.000000001Z\",\"price_level\":\"50001.25\",\"new_quantity\":\"2.50000000\"}]}]}"})";
    FVL_CHECK(coinbase.normalize(coinbase_record, updates) == AdapterStatus::Accepted);
    FVL_CHECK(updates.size() == 1);
    FVL_CHECK(updates.front().venue_id == 2);
    FVL_CHECK(updates.front().side == Side::Ask);
    FVL_CHECK(updates.front().price_ticks == 5'000'125);
    FVL_CHECK(updates.front().quantity == 250'000'000);
    FVL_CHECK(updates.front().exchange_timestamp_ns == 1'704'067'200'000'000'001);
    FVL_CHECK(updates.front().sequence_number == 1);

    const std::string okx_record =
        R"({"local_receipt_timestamp_ns":1704067200000000300,"raw_payload":"{\"arg\":{\"channel\":\"books\"},\"data\":[{\"ts\":\"1704067200000\",\"seqId\":13,\"bids\":[],\"asks\":[[\"50001.2\",\"3.75000000\",\"0\",\"1\"]]}]}"})";
    FVL_CHECK(okx.normalize(okx_record, updates) == AdapterStatus::Accepted);
    FVL_CHECK(updates.size() == 1);
    FVL_CHECK(updates.front().venue_id == 3);
    FVL_CHECK(updates.front().side == Side::Ask);
    FVL_CHECK(updates.front().price_ticks == 500'012);
    FVL_CHECK(updates.front().quantity == 375'000'000);
    FVL_CHECK(updates.front().exchange_timestamp_ns == 1'704'067'200'000'000'000);
    FVL_CHECK(updates.front().sequence_number == 1);

    FVL_CHECK(binance.normalize("{}", updates) == AdapterStatus::Malformed);
    FVL_CHECK(coinbase.normalize(
                  R"({"local_receipt_timestamp_ns":1,"raw_payload":"{\"channel\":\"market_trades\"}"})",
                  updates) == AdapterStatus::Unsupported);
    FVL_CHECK(okx.normalize(
                  R"({"local_receipt_timestamp_ns":1,"raw_payload":"{\"event\":\"subscribe\",\"arg\":{\"channel\":\"books\"}}"})",
                  updates) == AdapterStatus::Unsupported);
    return true;
}

bool test_fixture_conversion_and_replay() {
    const auto output =
        std::filesystem::temp_directory_path() / "crossvenuefairvalue-multi-venue.csv";
    std::filesystem::remove(output);
    const auto conversion = fairvaluelab::convert_capture_directory(FVL_MULTI_FIXTURE_PATH, output);
    FVL_CHECK(conversion.size() == 3);
    for (const auto& [venue_id, stats] : conversion) {
        FVL_CHECK(venue_id >= 1 && venue_id <= 3);
        FVL_CHECK(stats.accepted == 753);
        FVL_CHECK(stats.malformed == 1);
        FVL_CHECK(stats.unsupported == 1);
    }

    std::ifstream sorted{output};
    FVL_CHECK(sorted.good());
    std::string line;
    FVL_CHECK(static_cast<bool>(std::getline(sorted, line)));
    std::uint64_t previous_timestamp = 0;
    std::size_t row_count = 0;
    while (std::getline(sorted, line)) {
        std::istringstream row{line};
        std::string field;
        for (std::size_t column = 0; column < 4; ++column) {
            FVL_CHECK(static_cast<bool>(std::getline(row, field, ',')));
        }
        const auto timestamp = std::stoull(field);
        FVL_CHECK(timestamp >= previous_timestamp);
        previous_timestamp = timestamp;
        ++row_count;
    }
    FVL_CHECK(row_count == 2'259);
    sorted.close();

    std::ifstream normalized{output};
    FVL_CHECK(normalized.good());
    std::ostringstream snapshots;
    const auto replay = fairvaluelab::replay_normalized_log(normalized, 500, &snapshots);
    FVL_CHECK(replay.size() == 3);
    for (const auto& [venue_id, stats] : replay) {
        FVL_CHECK(venue_id >= 1 && venue_id <= 3);
        FVL_CHECK(stats.accepted == 750);
        FVL_CHECK(stats.duplicate == 1);
        FVL_CHECK(stats.stale == 1);
        FVL_CHECK(stats.gapped == 1);
        FVL_CHECK(stats.malformed == 0);
    }
    FVL_CHECK(snapshots.str().find("venue 1") != std::string::npos);
    FVL_CHECK(snapshots.str().find("venue 2") != std::string::npos);
    FVL_CHECK(snapshots.str().find("venue 3") != std::string::npos);

    normalized.close();
    std::filesystem::remove(output);
    return true;
}

bool test_malformed_normalized_row() {
    std::istringstream input{
        "sequence_number,venue_id,exchange_timestamp_ns,local_receipt_timestamp_ns,side,"
        "price_ticks,quantity\n"
        "1,9,1,1,B,100,10\n"
        "invalid,9,2,2,B,101,10\n"};
    const auto report = fairvaluelab::replay_normalized_log(input, 10);
    FVL_CHECK(report.at(9).accepted == 1);
    FVL_CHECK(report.at(9).malformed == 1);
    return true;
}

struct TestCase {
    std::string_view name;
    bool (*run)();
};

int main() {
    constexpr std::array tests{
        TestCase{"concrete adapters", test_concrete_adapters},
        TestCase{"fixture conversion and replay", test_fixture_conversion_and_replay},
        TestCase{"malformed normalized row", test_malformed_normalized_row},
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
