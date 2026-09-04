#pragma once

#include "fairvaluelab/market_event.hpp"

#include <cstdint>
#include <filesystem>
#include <map>

namespace fairvaluelab {

struct ConversionStats {
    std::uint64_t accepted{};
    std::uint64_t malformed{};
    std::uint64_t unsupported{};
};

using ConversionReport = std::map<VenueId, ConversionStats>;

[[nodiscard]] ConversionReport convert_capture_directory(const std::filesystem::path& input,
                                                          const std::filesystem::path& output);

}
