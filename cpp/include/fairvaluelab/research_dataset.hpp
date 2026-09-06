#pragma once

#include "fairvaluelab/research_sampler.hpp"

#include <cstdint>
#include <iosfwd>
#include <vector>

namespace fairvaluelab {

struct ResearchDatasetConfig {
    ResearchSamplerConfig sampler;
    std::vector<TimestampNs> horizons_ns{
        10'000'000, 50'000'000, 100'000'000, 250'000'000, 1'000'000'000};
    TargetAlignmentConfig target_alignment;
};

struct ResearchDatasetReport {
    std::uint64_t input_events{};
    std::uint64_t sample_rows{};
    std::size_t venue_count{};
    std::size_t pair_count{};
};

[[nodiscard]] ResearchDatasetReport generate_research_dataset_csv(
    std::istream& input, std::ostream& output,
    ResearchDatasetConfig config = ResearchDatasetConfig{});

} // namespace fairvaluelab
