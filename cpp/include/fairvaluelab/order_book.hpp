#pragma once

#include "fairvaluelab/book_side.hpp"
#include "fairvaluelab/market_event.hpp"
#include "fairvaluelab/price_level.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace fairvaluelab {

enum class UpdateStatus : std::uint8_t {
    Accepted,
    Duplicate,
    Stale,
    SequenceGap,
    InvalidSide,
};

struct ApplyResult {
    UpdateStatus status{UpdateStatus::Accepted};
    LevelChange change{LevelChange::None};

    [[nodiscard]] bool accepted() const noexcept { return status == UpdateStatus::Accepted; }
};

class OrderBook {
  public:
    explicit OrderBook(std::size_t depth = BookSide::maximum_depth);

    [[nodiscard]] ApplyResult apply(const BookUpdate& update) noexcept;
    void apply_snapshot(std::span<const PriceLevel> bids, std::span<const PriceLevel> asks,
                        SequenceNumber sequence_number) noexcept;
    void reset() noexcept;

    [[nodiscard]] std::optional<PriceLevel> best_bid() const noexcept;
    [[nodiscard]] std::optional<PriceLevel> best_ask() const noexcept;
    [[nodiscard]] std::optional<PriceTicks> spread() const noexcept;
    [[nodiscard]] std::optional<double> mid_price() const noexcept;
    [[nodiscard]] std::optional<double> microprice() const noexcept;
    [[nodiscard]] std::optional<double> depth_imbalance(std::size_t top_k) const noexcept;

    [[nodiscard]] std::span<const PriceLevel> bids() const noexcept;
    [[nodiscard]] std::span<const PriceLevel> asks() const noexcept;
    [[nodiscard]] std::optional<SequenceNumber> last_sequence_number() const noexcept;
    [[nodiscard]] std::size_t capacity_per_side() const noexcept;

  private:
    BookSide bids_;
    BookSide asks_;
    std::optional<SequenceNumber> last_sequence_number_;
};

} // namespace fairvaluelab
