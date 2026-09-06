#include "fairvaluelab/research_dataset.hpp"

#include <charconv>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace {

fairvaluelab::TimestampNs parse_unsigned(const std::string_view value,
                                         const bool allow_zero = false) {
    fairvaluelab::TimestampNs parsed{};
    const auto [end, error] =
        std::from_chars(value.data(), value.data() + value.size(), parsed);
    if (error != std::errc{} || end != value.data() + value.size() ||
        (!allow_zero && parsed == 0)) {
        throw std::invalid_argument("invalid unsigned integer: " + std::string{value});
    }
    return parsed;
}

std::vector<fairvaluelab::TimestampNs> parse_horizons(std::string_view value) {
    std::vector<fairvaluelab::TimestampNs> horizons;
    while (!value.empty()) {
        const auto separator = value.find(',');
        const auto field = value.substr(0, separator);
        horizons.push_back(parse_unsigned(field));
        if (separator == std::string_view::npos) {
            break;
        }
        value.remove_prefix(separator + 1);
    }
    if (horizons.empty()) {
        throw std::invalid_argument("at least one target horizon is required");
    }
    return horizons;
}

void print_usage() {
    std::cerr
        << "usage: fvl_dataset --input normalized.csv --output dataset.csv "
           "[--sampling clock|event] [--clock-ns N] [--max-staleness-ns N] "
           "[--horizons-ns N,N,...] [--max-target-delay-ns N] "
           "[--discard-missing-targets]\n";
}

} // namespace

int main(const int argc, const char* const argv[]) {
    std::filesystem::path input_path;
    std::filesystem::path output_path;
    fairvaluelab::ResearchDatasetConfig config;
    try {
        for (int index = 1; index < argc; ++index) {
            const std::string_view option{argv[index]};
            if (option == "--discard-missing-targets") {
                config.target_alignment.missing_target_policy =
                    fairvaluelab::MissingTargetPolicy::DiscardRow;
                continue;
            }
            if (index + 1 == argc) {
                throw std::invalid_argument("missing option value");
            }
            const std::string_view value{argv[++index]};
            if (option == "--input") {
                input_path = value;
            } else if (option == "--output") {
                output_path = value;
            } else if (option == "--sampling") {
                if (value == "clock") {
                    config.sampler.sample_kind = fairvaluelab::SampleKind::Clock;
                } else if (value == "event") {
                    config.sampler.sample_kind = fairvaluelab::SampleKind::Event;
                } else {
                    throw std::invalid_argument("sampling must be clock or event");
                }
            } else if (option == "--clock-ns") {
                config.sampler.feature_emitter.clock_interval_ns = parse_unsigned(value);
            } else if (option == "--max-staleness-ns") {
                config.sampler.synchronizer.max_staleness_ns = parse_unsigned(value, true);
            } else if (option == "--horizons-ns") {
                config.horizons_ns = parse_horizons(value);
            } else if (option == "--max-target-delay-ns") {
                config.target_alignment.max_target_delay_ns = parse_unsigned(value, true);
            } else {
                throw std::invalid_argument("unknown option: " + std::string{option});
            }
        }
        if (input_path.empty() || output_path.empty()) {
            throw std::invalid_argument("input and output paths are required");
        }
    } catch (const std::exception& error) {
        print_usage();
        std::cerr << error.what() << '\n';
        return 1;
    }

    std::ifstream input{input_path};
    if (!input) {
        std::cerr << "failed to open normalized input: " << input_path << '\n';
        return 1;
    }
    if (output_path.has_parent_path()) {
        std::error_code error;
        std::filesystem::create_directories(output_path.parent_path(), error);
        if (error) {
            std::cerr << "failed to create output directory: " << error.message() << '\n';
            return 1;
        }
    }
    std::ofstream output{output_path};
    if (!output) {
        std::cerr << "failed to open dataset output: " << output_path << '\n';
        return 1;
    }

    try {
        const auto report = fairvaluelab::generate_research_dataset_csv(input, output, config);
        std::cout << "input events: " << report.input_events << '\n'
                  << "sample rows: " << report.sample_rows << '\n'
                  << "venues: " << report.venue_count << '\n'
                  << "pairs: " << report.pair_count << '\n';
    } catch (const std::exception& error) {
        std::cerr << "dataset generation failed: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
