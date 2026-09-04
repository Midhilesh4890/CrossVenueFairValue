#include "fairvaluelab/multi_replay.hpp"

#include <charconv>
#include <cstddef>
#include <fstream>
#include <iostream>
#include <string_view>

int main(const int argc, const char* const argv[]) {
    if (argc != 2 && argc != 4) {
        std::cerr << "usage: fvl_multi_replay <normalized.csv> "
                     "[--snapshot-interval <events>]\n";
        return 1;
    }

    std::size_t snapshot_interval = 1'000;
    if (argc == 4) {
        const std::string_view option{argv[2]};
        const std::string_view value{argv[3]};
        const auto [end, error] =
            std::from_chars(value.data(), value.data() + value.size(), snapshot_interval);
        if (option != "--snapshot-interval" || error != std::errc{} ||
            end != value.data() + value.size() || snapshot_interval == 0) {
            std::cerr << "invalid snapshot interval\n";
            return 1;
        }
    }

    std::ifstream input{argv[1]};
    if (!input) {
        std::cerr << "failed to open normalized log: " << argv[1] << '\n';
        return 1;
    }

    try {
        const auto report =
            fairvaluelab::replay_normalized_log(input, snapshot_interval, &std::cout);
        for (const auto& [venue_id, stats] : report) {
            std::cout << "venue " << venue_id << ": accepted=" << stats.accepted
                      << " duplicate=" << stats.duplicate << " stale=" << stats.stale
                      << " gapped=" << stats.gapped << " malformed=" << stats.malformed << '\n';
        }
    } catch (const std::exception& error) {
        std::cerr << "multi-venue replay failed: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
