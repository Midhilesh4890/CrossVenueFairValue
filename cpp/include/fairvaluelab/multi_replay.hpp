#pragma once

#include "fairvaluelab/market_event.hpp"

#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <map>

namespace fairvaluelab {

struct VenueReplayStats {
    std::uint64_t accepted{};
    std::uint64_t duplicate{};
    std::uint64_t stale{};
    std::uint64_t gapped{};
    std::uint64_t malformed{};
};

using ReplayReport = std::map<VenueId, VenueReplayStats>;

[[nodiscard]] ReplayReport replay_normalized_log(std::istream& input,
                                                 std::size_t snapshot_interval,
                                                 std::ostream* snapshots = nullptr);

}
