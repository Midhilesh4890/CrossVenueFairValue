#pragma once

#include "fairvaluelab/market_event.hpp"
#include "fairvaluelab/price_level.hpp"

#include <array>
#include <cstddef>
#include <optional>
#include <span>

namespace fairvaluelab {

enum class LevelChange : std::uint8_t {
    None,
    Inserted,
    Updated,
    Removed,
    NotFound,
    Discarded,
};

class BookSide {
  public:
    static constexpr std::size_t maximum_depth = 64;

    explicit BookSide(Side side, std::size_t depth = maximum_depth);

    [[nodiscard]] LevelChange apply(PriceTicks price_ticks, Quantity quantity) noexcept;
    void clear() noexcept;

    [[nodiscard]] std::optional<PriceLevel> best() const noexcept;
    [[nodiscard]] std::span<const PriceLevel> levels() const noexcept;
    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] std::size_t capacity() const noexcept;
    [[nodiscard]] Side side() const noexcept;

  private:
    [[nodiscard]] bool is_better(PriceTicks lhs, PriceTicks rhs) const noexcept;

    Side side_;
    std::size_t depth_;
    std::size_t size_{0};
    std::array<PriceLevel, maximum_depth> levels_{};
};

} // namespace fairvaluelab
