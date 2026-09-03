#include "fairvaluelab/order_book.hpp"

#include <array>
#include <bit>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#ifndef FVL_FIXTURE_PATH
#define FVL_FIXTURE_PATH "data/fixtures/order_book_updates.csv"
#endif

using fairvaluelab::BookUpdate;
using fairvaluelab::OrderBook;
using fairvaluelab::PriceLevel;
using fairvaluelab::Side;

using DoubleBits = std::array<std::byte, sizeof(double)>;

struct Snapshot {
    std::optional<PriceLevel> best_bid;
    std::optional<PriceLevel> best_ask;
    std::optional<DoubleBits> mid;
    std::optional<DoubleBits> microprice;

    bool operator==(const Snapshot&) const = default;
};

template <typename Integer>
Integer parse_integer(const std::string_view value, const std::size_t line_number) {
    Integer parsed{};
    const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), parsed);
    if (error != std::errc{} || end != value.data() + value.size()) {
        throw std::runtime_error("invalid integer on CSV line " + std::to_string(line_number));
    }
    return parsed;
}

std::array<std::string_view, 6> split_row(const std::string& line, const std::size_t line_number) {
    std::array<std::string_view, 6> columns{};
    const std::string_view row{line};
    std::size_t begin = 0;
    for (std::size_t column = 0; column < columns.size(); ++column) {
        const auto separator = row.find(',', begin);
        const bool is_last = column + 1 == columns.size();
        if ((separator == std::string_view::npos) != is_last) {
            throw std::runtime_error("expected six columns on CSV line " +
                                     std::to_string(line_number));
        }
        const auto end = separator == std::string_view::npos ? row.size() : separator;
        columns[column] = row.substr(begin, end - begin);
        begin = end + 1;
    }
    if (!columns.back().empty() && columns.back().back() == '\r') {
        columns.back().remove_suffix(1);
    }
    return columns;
}

Side parse_side(const std::string_view value, const std::size_t line_number) {
    if (value == "B") {
        return Side::Bid;
    }
    if (value == "A") {
        return Side::Ask;
    }
    throw std::runtime_error("invalid side on CSV line " + std::to_string(line_number));
}

BookUpdate parse_update(const std::string& line, const std::size_t line_number) {
    const auto columns = split_row(line, line_number);
    return BookUpdate{
        parse_side(columns[3], line_number),
        parse_integer<std::int64_t>(columns[4], line_number),
        parse_integer<std::uint64_t>(columns[5], line_number),
        parse_integer<std::uint64_t>(columns[1], line_number),
        parse_integer<std::uint64_t>(columns[2], line_number),
        parse_integer<std::uint64_t>(columns[0], line_number),
    };
}

std::optional<DoubleBits> bits(const std::optional<double> value) {
    if (!value.has_value()) {
        return std::nullopt;
    }
    return std::bit_cast<DoubleBits>(*value);
}

std::vector<Snapshot> replay_once() {
    std::ifstream input{FVL_FIXTURE_PATH};
    if (!input) {
        throw std::runtime_error("failed to open replay fixture");
    }

    std::string line;
    if (!std::getline(input, line)) {
        throw std::runtime_error("replay fixture is empty");
    }

    OrderBook book;
    std::vector<Snapshot> snapshots;
    std::size_t line_number = 1;
    while (std::getline(input, line)) {
        ++line_number;
        if (line.empty()) {
            continue;
        }
        if (!book.apply(parse_update(line, line_number)).accepted()) {
            throw std::runtime_error("replay update was rejected on CSV line " +
                                     std::to_string(line_number));
        }
        snapshots.push_back(Snapshot{book.best_bid(), book.best_ask(), bits(book.mid_price()),
                                     bits(book.microprice())});
    }

    if (snapshots.empty()) {
        throw std::runtime_error("replay fixture has no updates");
    }
    return snapshots;
}

int main() {
    try {
        const auto first = replay_once();
        const auto second = replay_once();
        if (first != second) {
            std::cerr << "replay snapshots are not bitwise deterministic\n";
            return 1;
        }
        std::cout << first.size() << " deterministic snapshots verified\n";
    } catch (const std::exception& error) {
        std::cerr << "determinism test failed: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
