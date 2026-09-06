#include "fairvaluelab/research_dataset.hpp"
#include "fairvaluelab/normalized_csv.hpp"

#include <array>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>

namespace {

#define FVL_CHECK(condition)                                                                       \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            std::cerr << __FILE__ << ':' << __LINE__ << ": check failed: " #condition << '\n';     \
            return false;                                                                          \
        }                                                                                          \
    } while (false)

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
    FVL_CHECK(csv.find("venue_2_ofi_time_window") != std::string::npos);
    FVL_CHECK(csv.find("pair_1_2_mid_difference") != std::string::npos);
    FVL_CHECK(csv.find("future_consolidated_mid_10") != std::string::npos);
    FVL_CHECK(csv.find("clock,110,") != std::string::npos);
    FVL_CHECK(csv.find("clock,120,") != std::string::npos);
    FVL_CHECK(csv.find("clock,130,") != std::string::npos);
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

struct TestCase {
    std::string_view name;
    bool (*run)();
};

} // namespace

int main() {
    constexpr std::array tests{
        TestCase{"research dataset CSV", test_research_dataset_csv},
        TestCase{"dataset rejects bad input", test_dataset_rejects_bad_input},
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
