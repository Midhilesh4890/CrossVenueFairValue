#include "fairvaluelab/order_book.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace fairvaluelab {

BookSide::BookSide(const Side side, const std::size_t depth) : side_(side), depth_(depth) {
    if ((side != Side::Bid && side != Side::Ask) || depth == 0 || depth > maximum_depth) {
        throw std::invalid_argument("invalid book side or depth");
    }
}

LevelChange BookSide::apply(const PriceTicks price_ticks, const Quantity quantity) noexcept {
    std::size_t existing = 0;
    while (existing < size_ && levels_[existing].price_ticks != price_ticks) {
        ++existing;
    }

    if (existing < size_) {
        if (quantity != 0) {
            levels_[existing].quantity = quantity;
            return LevelChange::Updated;
        }

        for (std::size_t index = existing; index + 1 < size_; ++index) {
            levels_[index] = levels_[index + 1];
        }
        --size_;
        levels_[size_] = {};
        return LevelChange::Removed;
    }

    if (quantity == 0) {
        return LevelChange::NotFound;
    }

    std::size_t insertion = 0;
    while (insertion < size_ && is_better(levels_[insertion].price_ticks, price_ticks)) {
        ++insertion;
    }

    if (size_ == depth_ && insertion == size_) {
        return LevelChange::Discarded;
    }

    if (size_ < depth_) {
        for (std::size_t index = size_; index > insertion; --index) {
            levels_[index] = levels_[index - 1];
        }
        ++size_;
    } else {
        for (std::size_t index = size_ - 1; index > insertion; --index) {
            levels_[index] = levels_[index - 1];
        }
    }

    levels_[insertion] = PriceLevel{price_ticks, quantity};
    return LevelChange::Inserted;
}

void BookSide::clear() noexcept {
    std::fill_n(levels_.begin(), size_, PriceLevel{});
    size_ = 0;
}

std::optional<PriceLevel> BookSide::best() const noexcept {
    if (size_ == 0) {
        return std::nullopt;
    }
    return levels_.front();
}

std::span<const PriceLevel> BookSide::levels() const noexcept {
    return {levels_.data(), size_};
}

std::size_t BookSide::size() const noexcept { return size_; }

std::size_t BookSide::capacity() const noexcept { return depth_; }

Side BookSide::side() const noexcept { return side_; }

bool BookSide::is_better(const PriceTicks lhs, const PriceTicks rhs) const noexcept {
    return side_ == Side::Bid ? lhs > rhs : lhs < rhs;
}

OrderBook::OrderBook(const std::size_t depth) : bids_(Side::Bid, depth), asks_(Side::Ask, depth) {}

ApplyResult OrderBook::apply(const BookUpdate& update) noexcept {
    if (update.side != Side::Bid && update.side != Side::Ask) {
        return {UpdateStatus::InvalidSide, LevelChange::None};
    }

    if (last_sequence_number_.has_value()) {
        const auto last = *last_sequence_number_;
        if (update.sequence_number == last) {
            return {UpdateStatus::Duplicate, LevelChange::None};
        }
        if (update.sequence_number < last) {
            return {UpdateStatus::Stale, LevelChange::None};
        }
        if (last == std::numeric_limits<SequenceNumber>::max() ||
            update.sequence_number != last + 1) {
            return {UpdateStatus::SequenceGap, LevelChange::None};
        }
    }

    const auto change = update.side == Side::Bid
                            ? bids_.apply(update.price_ticks, update.quantity)
                            : asks_.apply(update.price_ticks, update.quantity);
    last_sequence_number_ = update.sequence_number;
    return {UpdateStatus::Accepted, change};
}

void OrderBook::apply_snapshot(const std::span<const PriceLevel> bids,
                               const std::span<const PriceLevel> asks,
                               const SequenceNumber sequence_number) noexcept {
    bids_.clear();
    asks_.clear();
    for (const auto& level : bids) {
        static_cast<void>(bids_.apply(level.price_ticks, level.quantity));
    }
    for (const auto& level : asks) {
        static_cast<void>(asks_.apply(level.price_ticks, level.quantity));
    }
    last_sequence_number_ = sequence_number;
}

void OrderBook::reset() noexcept {
    bids_.clear();
    asks_.clear();
    last_sequence_number_.reset();
}

std::optional<PriceLevel> OrderBook::best_bid() const noexcept { return bids_.best(); }

std::optional<PriceLevel> OrderBook::best_ask() const noexcept { return asks_.best(); }

std::optional<PriceTicks> OrderBook::spread() const noexcept {
    const auto bid = best_bid();
    const auto ask = best_ask();
    if (!bid.has_value() || !ask.has_value()) {
        return std::nullopt;
    }

    const auto ask_price = ask->price_ticks;
    const auto bid_price = bid->price_ticks;
    if ((bid_price > 0 && ask_price < std::numeric_limits<PriceTicks>::min() + bid_price) ||
        (bid_price < 0 && ask_price > std::numeric_limits<PriceTicks>::max() + bid_price)) {
        return std::nullopt;
    }
    return ask_price - bid_price;
}

std::optional<double> OrderBook::mid_price() const noexcept {
    const auto bid = best_bid();
    const auto ask = best_ask();
    if (!bid.has_value() || !ask.has_value()) {
        return std::nullopt;
    }

    const auto total = static_cast<long double>(bid->price_ticks) +
                       static_cast<long double>(ask->price_ticks);
    return static_cast<double>(total / 2.0L);
}

std::optional<double> OrderBook::microprice() const noexcept {
    const auto bid = best_bid();
    const auto ask = best_ask();
    if (!bid.has_value() || !ask.has_value()) {
        return std::nullopt;
    }

    const auto bid_quantity = static_cast<long double>(bid->quantity);
    const auto ask_quantity = static_cast<long double>(ask->quantity);
    const auto denominator = bid_quantity + ask_quantity;
    if (denominator == 0.0L) {
        return std::nullopt;
    }

    const auto numerator = static_cast<long double>(ask->price_ticks) * bid_quantity +
                           static_cast<long double>(bid->price_ticks) * ask_quantity;
    return static_cast<double>(numerator / denominator);
}

std::optional<double> OrderBook::depth_imbalance(const std::size_t top_k) const noexcept {
    if (top_k == 0 || bids_.size() == 0 || asks_.size() == 0) {
        return std::nullopt;
    }

    long double bid_depth = 0.0L;
    long double ask_depth = 0.0L;
    const auto bid_levels = bids_.levels();
    const auto ask_levels = asks_.levels();
    for (std::size_t index = 0; index < std::min(top_k, bid_levels.size()); ++index) {
        bid_depth += static_cast<long double>(bid_levels[index].quantity);
    }
    for (std::size_t index = 0; index < std::min(top_k, ask_levels.size()); ++index) {
        ask_depth += static_cast<long double>(ask_levels[index].quantity);
    }

    const auto denominator = bid_depth + ask_depth;
    if (denominator == 0.0L) {
        return std::nullopt;
    }
    return static_cast<double>((bid_depth - ask_depth) / denominator);
}

std::span<const PriceLevel> OrderBook::bids() const noexcept { return bids_.levels(); }

std::span<const PriceLevel> OrderBook::asks() const noexcept { return asks_.levels(); }

std::optional<SequenceNumber> OrderBook::last_sequence_number() const noexcept {
    return last_sequence_number_;
}

std::size_t OrderBook::capacity_per_side() const noexcept { return bids_.capacity(); }

} // namespace fairvaluelab
