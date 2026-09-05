#include "fairvaluelab/feature_emitter.hpp"

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>

namespace {

std::uint64_t parse_positive(const std::string_view value) {
    std::uint64_t parsed = 0;
    const auto [end, error] =
        std::from_chars(value.data(), value.data() + value.size(), parsed);
    if (error != std::errc{} || end != value.data() + value.size() || parsed == 0) {
        throw std::invalid_argument("invalid positive integer");
    }
    return parsed;
}

}

int main(const int argc, const char* const argv[]) {
    if (argc < 3 || (argc - 3) % 2 != 0) {
        std::cerr << "usage: fvl_features <normalized.csv> <features.csv> "
                     "[--clock-interval-ns N] [--event-window N] [--time-window-ns N] "
                     "[--band-ticks N] [--venue-capacity N]\n";
        return 1;
    }

    fairvaluelab::FeatureEmitterConfig config;
    try {
        for (int index = 3; index < argc; index += 2) {
            const std::string_view option{argv[index]};
            const auto value = parse_positive(argv[index + 1]);
            if (option == "--clock-interval-ns") {
                config.clock_interval_ns = value;
            } else if (option == "--event-window") {
                if (value > std::numeric_limits<std::size_t>::max()) {
                    throw std::invalid_argument("event window is too large");
                }
                config.event_window = static_cast<std::size_t>(value);
            } else if (option == "--time-window-ns") {
                config.time_window_ns = value;
            } else if (option == "--venue-capacity") {
                if (value > std::numeric_limits<std::size_t>::max()) {
                    throw std::invalid_argument("venue capacity is too large");
                }
                config.venue_capacity = static_cast<std::size_t>(value);
            } else if (option == "--band-ticks") {
                config.band_ticks = value;
            } else {
                throw std::invalid_argument("unknown option");
            }
        }
    } catch (const std::exception& error) {
        std::cerr << "invalid feature configuration: " << error.what() << '\n';
        return 1;
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
        fairvaluelab::FeatureEmitter emitter{config};
        const auto rows = fairvaluelab::write_feature_csv(input, output, emitter);
        std::cout << "feature rows: " << rows << '\n';
        for (const auto& dropped : emitter.dropped_entries()) {
            std::cout << "venue " << dropped.venue_id
                      << " dropped order flow entries: " << dropped.order_flow
                      << " dropped trade flow entries: " << dropped.trade_flow << '\n';
        }
    } catch (const std::exception& error) {
        std::cerr << "feature emission failed: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
