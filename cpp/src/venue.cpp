#include "fairvaluelab/venue.hpp"

#include <cstdint>
#include <limits>
#include <numeric>

static std::uint64_t unsigned_magnitude(const std::int64_t value) noexcept {
    if (value >= 0) {
        return static_cast<std::uint64_t>(value);
    }
    return static_cast<std::uint64_t>(-(value + 1)) + 1;
}

static void cancel_factor(std::uint64_t& numerator, std::uint64_t& denominator) noexcept {
    const auto factor = std::gcd(numerator, denominator);
    numerator /= factor;
    denominator /= factor;
}

std::optional<fairvaluelab::PriceTicks>
fairvaluelab::price_to_ticks(const Rational price, const Rational tick_size) noexcept {
    if (price.denominator == 0 || tick_size.numerator <= 0 || tick_size.denominator == 0) {
        return std::nullopt;
    }

    std::uint64_t price_magnitude = unsigned_magnitude(price.numerator);
    std::uint64_t tick_denominator = tick_size.denominator;
    std::uint64_t price_denominator = price.denominator;
    std::uint64_t tick_numerator = static_cast<std::uint64_t>(tick_size.numerator);

    cancel_factor(price_magnitude, price_denominator);
    cancel_factor(price_magnitude, tick_numerator);
    cancel_factor(tick_denominator, price_denominator);
    cancel_factor(tick_denominator, tick_numerator);

    if (price_denominator != 1 || tick_numerator != 1) {
        return std::nullopt;
    }

    constexpr auto positive_limit =
        static_cast<std::uint64_t>(std::numeric_limits<PriceTicks>::max());
    constexpr auto negative_limit = positive_limit + 1;
    const auto result_limit = price.numerator < 0 ? negative_limit : positive_limit;
    if (price_magnitude != 0 && tick_denominator > result_limit / price_magnitude) {
        return std::nullopt;
    }

    const auto magnitude = price_magnitude * tick_denominator;
    if (price.numerator >= 0) {
        return static_cast<PriceTicks>(magnitude);
    }
    if (magnitude == negative_limit) {
        return std::numeric_limits<PriceTicks>::min();
    }
    return -static_cast<PriceTicks>(magnitude);
}

std::optional<fairvaluelab::Quantity>
fairvaluelab::scale_quantity(const Quantity raw_quantity, const Quantity scale_factor) noexcept {
    if (scale_factor == 0 || raw_quantity > std::numeric_limits<Quantity>::max() / scale_factor) {
        return std::nullopt;
    }
    return raw_quantity * scale_factor;
}
