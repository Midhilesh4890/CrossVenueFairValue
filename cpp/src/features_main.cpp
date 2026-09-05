#include "fairvaluelab/feature_emitter.hpp"

#include <charconv>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <string_view>

int main(const int argc, const char* const argv[]) {
    if (argc != 3 && argc != 5) {
        std::cerr << "usage: fvl_features <normalized.csv> <features.csv> "
                     "[--clock-interval-ns <nanoseconds>]\n";
        return 1;
    }

    std::uint64_t clock_interval_ns = 100'000'000;
    if (argc == 5) {
        const std::string_view option{argv[3]};
        const std::string_view value{argv[4]};
        const auto [end, error] =
            std::from_chars(value.data(), value.data() + value.size(), clock_interval_ns);
        if (option != "--clock-interval-ns" || error != std::errc{} ||
            end != value.data() + value.size() || clock_interval_ns == 0) {
            std::cerr << "invalid clock interval\n";
            return 1;
        }
    }

    std::ifstream input{argv[1]};
    if (!input) {
        std::cerr << "failed to open normalized log: " << argv[1] << '\n';
        return 1;
    }
    std::ofstream output{argv[2]};
    if (!output) {
        std::cerr << "failed to open feature output: " << argv[2] << '\n';
        return 1;
    }

    try {
        const auto rows = fairvaluelab::write_feature_csv(input, output, clock_interval_ns);
        std::cout << "feature rows: " << rows << '\n';
    } catch (const std::exception& error) {
        std::cerr << "feature emission failed: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
