#include "fairvaluelab/capture_converter.hpp"

#include <filesystem>
#include <iostream>

int main(const int argc, const char* const argv[]) {
    if (argc != 3) {
        std::cerr << "usage: fvl_convert_capture <capture-directory> <output.csv>\n";
        return 1;
    }

    try {
        const auto report = fairvaluelab::convert_capture_directory(
            std::filesystem::path{argv[1]}, std::filesystem::path{argv[2]});
        for (const auto& [venue_id, stats] : report) {
            std::cout << "venue " << venue_id << ": accepted=" << stats.accepted
                      << " malformed=" << stats.malformed
                      << " unsupported=" << stats.unsupported << '\n';
        }
    } catch (const std::exception& error) {
        std::cerr << "capture conversion failed: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
