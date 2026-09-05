#pragma once

#include "fairvaluelab/venue.hpp"
#include "fairvaluelab/venue_adapter.hpp"

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace fairvaluelab {

class BinanceAdapter final : public VenueAdapter {
  public:
    explicit BinanceAdapter(VenueConfig config);
    [[nodiscard]] AdapterStatus normalize(std::string_view raw_record,
                                          std::vector<BookUpdate>& output) const override;
    [[nodiscard]] AdapterStatus normalize_trades(std::string_view raw_record,
                                                 std::vector<Trade>& output) const override;

  private:
    VenueConfig config_;
    mutable std::optional<std::uint64_t> source_sequence_;
    mutable std::uint64_t normalized_sequence_{1};
};

class CoinbaseAdapter final : public VenueAdapter {
  public:
    explicit CoinbaseAdapter(VenueConfig config);
    [[nodiscard]] AdapterStatus normalize(std::string_view raw_record,
                                          std::vector<BookUpdate>& output) const override;
    [[nodiscard]] AdapterStatus normalize_trades(std::string_view raw_record,
                                                 std::vector<Trade>& output) const override;

  private:
    VenueConfig config_;
    mutable std::optional<std::uint64_t> source_sequence_;
    mutable std::uint64_t normalized_sequence_{1};
    mutable bool gap_pending_{};
};

class OkxAdapter final : public VenueAdapter {
  public:
    explicit OkxAdapter(VenueConfig config);
    [[nodiscard]] AdapterStatus normalize(std::string_view raw_record,
                                          std::vector<BookUpdate>& output) const override;
    [[nodiscard]] AdapterStatus normalize_trades(std::string_view raw_record,
                                                 std::vector<Trade>& output) const override;

  private:
    VenueConfig config_;
    mutable std::optional<std::uint64_t> source_sequence_;
    mutable std::uint64_t normalized_sequence_{1};
};

}
