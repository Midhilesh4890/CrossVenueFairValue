#pragma once

#include "fairvaluelab/market_event.hpp"

#include <cstdint>
#include <string_view>
#include <vector>

namespace fairvaluelab {

enum class AdapterStatus : std::uint8_t {
    Accepted,
    Malformed,
    Unsupported,
};

class VenueAdapter {
  public:
    virtual ~VenueAdapter() = default;

    [[nodiscard]] virtual AdapterStatus normalize(std::string_view raw_record,
                                                  std::vector<BookUpdate>& output) const = 0;
    [[nodiscard]] virtual AdapterStatus normalize_trades(std::string_view raw_record,
                                                         std::vector<Trade>& output) const = 0;
};

}
