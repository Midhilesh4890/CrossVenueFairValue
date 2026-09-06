#include "fairvaluelab/research_dataset.hpp"
#include "fairvaluelab/normalized_csv.hpp"

#include <algorithm>
#include <array>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#ifndef FVL_RESEARCH_FIXTURE_PATH
#define FVL_RESEARCH_FIXTURE_PATH "data/fixtures/multi_venue_updates.csv"
#endif

namespace {

#define FVL_CHECK(condition)                                                                       \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            std::cerr << __FILE__ << ':' << __LINE__ << ": check failed: " #condition << '\n';     \
            return false;                                                                          \
        }                                                                                          \
    } while (false)

std::vector<std::string_view> split(const std::string_view row) {
    std::vector<std::string_view> fields;
    std::size_t begin = 0;
    while (begin <= row.size()) {
        const auto separator = row.find(',', begin);
        fields.push_back(row.substr(begin, separator == std::string_view::npos
                                               ? row.size() - begin
                                               : separator - begin));
        if (separator == std::string_view::npos) {
            break;
        }
        begin = separator + 1;
    }
    return fields;
}

bool test_research_dataset_csv() {
    std::istringstream input{
        "event_type,sequence_number,venue_id,exchange_timestamp_ns,"
        "local_receipt_timestamp_ns,side,price_ticks,quantity\n"
        "book,1,1,99,100,B,100,10\n"
        "book,2,1,100,101,A,102,10\n"
        "book,1,2,101,102,B,200,20\n"
        "book,2,2,102,103,A,204,20\n"
        "book,3,1,129,130,B,100,12\n"};
    std::ostringstream output;
    fairvaluelab::ResearchDatasetConfig config;
    config.sampler.feature_emitter.clock_interval_ns = 10;
    config.sampler.feature_emitter.venue_capacity = 2;
    config.sampler.synchronizer.max_staleness_ns = 100;
    config.horizons_ns = {10};
    config.target_alignment.max_target_delay_ns = 0;
    const auto report = fairvaluelab::generate_research_dataset_csv(input, output, config);
    FVL_CHECK(report.input_events == 5);
    FVL_CHECK(report.sample_rows == 3);
    FVL_CHECK(report.venue_count == 2);
    FVL_CHECK(report.pair_count == 1);

    const auto csv = output.str();
    FVL_CHECK(csv.starts_with("sample_kind,sample_timestamp_ns,consolidated_mid"));
    FVL_CHECK(csv.find("venue_1_mid_minus_consolidated_mid") != std::string::npos);
    FVL_CHECK(csv.find("venue_1_latest_local_receipt_timestamp_ns") != std::string::npos);
    FVL_CHECK(csv.find("venue_1_latest_exchange_timestamp_ns") != std::string::npos);
    FVL_CHECK(csv.find("venue_2_ofi_time_window") != std::string::npos);
    FVL_CHECK(csv.find("pair_1_2_mid_difference") != std::string::npos);
    FVL_CHECK(csv.find("future_consolidated_mid_10") != std::string::npos);
    FVL_CHECK(csv.find("target_timestamp_10,target_delay_ns_10") != std::string::npos);
    FVL_CHECK(csv.find("clock,110,") != std::string::npos);
    FVL_CHECK(csv.find("clock,120,") != std::string::npos);
    FVL_CHECK(csv.find("clock,130,") != std::string::npos);

    std::istringstream rows{csv};
    std::string header_line;
    std::string first_row;
    FVL_CHECK(static_cast<bool>(std::getline(rows, header_line)));
    FVL_CHECK(static_cast<bool>(std::getline(rows, first_row)));
    const auto header_fields = split(header_line);
    const auto row_fields = split(first_row);
    FVL_CHECK(header_fields.size() == row_fields.size());
    const auto target_timestamp =
        std::find(header_fields.begin(), header_fields.end(), "target_timestamp_10");
    const auto target_delay =
        std::find(header_fields.begin(), header_fields.end(), "target_delay_ns_10");
    FVL_CHECK(target_timestamp != header_fields.end());
    FVL_CHECK(target_delay != header_fields.end());
    FVL_CHECK(row_fields[static_cast<std::size_t>(target_timestamp - header_fields.begin())] ==
              "120");
    FVL_CHECK(row_fields[static_cast<std::size_t>(target_delay - header_fields.begin())] == "0");
    return true;
}

bool test_dataset_rejects_bad_input() {
    for (const auto& input_text : {std::string{"bad header\n"},
                                   std::string{fairvaluelab::normalized_csv_header} + "\n"}) {
        std::istringstream input{input_text};
        std::ostringstream output;
        bool rejected = false;
        try {
            static_cast<void>(fairvaluelab::generate_research_dataset_csv(input, output));
        } catch (const std::runtime_error&) {
            rejected = true;
        }
        FVL_CHECK(rejected);
    }
    return true;
}

bool test_synthetic_multi_venue_fixture() {
    std::ifstream input{FVL_RESEARCH_FIXTURE_PATH};
    FVL_CHECK(input.good());
    std::ostringstream output;
    fairvaluelab::ResearchDatasetConfig config;
    config.sampler.feature_emitter.clock_interval_ns = 50'000'000;
    config.sampler.feature_emitter.venue_capacity = 3;
    config.sampler.synchronizer.max_staleness_ns = 100'000'000;
    config.horizons_ns = {50'000'000};
    config.target_alignment.max_target_delay_ns = 0;
    const auto report = fairvaluelab::generate_research_dataset_csv(input, output, config);
    FVL_CHECK(report.input_events == 1'107);
    FVL_CHECK(report.sample_rows == 320);
    FVL_CHECK(report.venue_count == 3);
    FVL_CHECK(report.pair_count == 3);

    std::istringstream rows{output.str()};
    std::string line;
    FVL_CHECK(static_cast<bool>(std::getline(rows, line)));
    const auto header = split(line);
    const auto sample_timestamp =
        std::find(header.begin(), header.end(), "sample_timestamp_ns") - header.begin();
    const auto venue_three_fresh =
        std::find(header.begin(), header.end(), "venue_3_fresh") - header.begin();
    FVL_CHECK(sample_timestamp < static_cast<std::ptrdiff_t>(header.size()));
    FVL_CHECK(venue_three_fresh < static_cast<std::ptrdiff_t>(header.size()));
    bool saw_stale = false;
    bool saw_recovered = false;
    while (std::getline(rows, line)) {
        const auto fields = split(line);
        if (fields[static_cast<std::size_t>(sample_timestamp)] == "10100000000") {
            saw_stale = fields[static_cast<std::size_t>(venue_three_fresh)] == "0";
        }
        if (fields[static_cast<std::size_t>(sample_timestamp)] == "10500000000") {
            saw_recovered = fields[static_cast<std::size_t>(venue_three_fresh)] == "1";
        }
    }
    FVL_CHECK(saw_stale);
    FVL_CHECK(saw_recovered);
    return true;
}

struct TestCase {
    std::string_view name;
    bool (*run)();
};

} // namespace

int main() {
    constexpr std::array tests{
        TestCase{"research dataset CSV", test_research_dataset_csv},
        TestCase{"dataset rejects bad input", test_dataset_rejects_bad_input},
        TestCase{"synthetic multi-venue fixture", test_synthetic_multi_venue_fixture},
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
