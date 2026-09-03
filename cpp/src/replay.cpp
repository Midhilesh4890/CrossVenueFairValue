#include "fairvaluelab/order_book.hpp"

#include <array>
#include <charconv>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

using fairvaluelab::BookUpdate;
using fairvaluelab::OrderBook;
using fairvaluelab::PriceLevel;
using fairvaluelab::Side;
using fairvaluelab::UpdateStatus;

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
    if (value == "B" || value == "bid" || value == "Bid") {
        return Side::Bid;
    }
    if (value == "A" || value == "ask" || value == "Ask") {
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

void print_level(const char* label, const std::optional<PriceLevel> level) {
    std::cout << label << ": ";
    if (!level.has_value()) {
        std::cout << "unavailable\n";
        return;
    }
    std::cout << level->price_ticks << " (quantity " << level->quantity << ")\n";
}

template <typename Value>
void print_optional(const char* label, const std::optional<Value> value) {
    std::cout << label << ": ";
    if (value.has_value()) {
        std::cout << *value << '\n';
    } else {
        std::cout << "unavailable\n";
    }
}

} // namespace

int main(const int argc, const char* const argv[]) {
    const std::string path = argc > 1 ? argv[1] : "data/fixtures/order_book_updates.csv";
    std::ifstream input(path);
    if (!input) {
        std::cerr << "failed to open replay fixture: " << path << '\n';
        return 1;
    }

    try {
        OrderBook book;
        std::uint64_t processed = 0;
        std::uint64_t accepted = 0;
        std::uint64_t duplicates = 0;
        std::uint64_t stale = 0;
        std::uint64_t gaps = 0;
        std::uint64_t invalid = 0;
        std::string line;
        std::size_t line_number = 0;

        while (std::getline(input, line)) {
            ++line_number;
            if (line_number == 1 || line.empty()) {
                continue;
            }

            ++processed;
            const auto result = book.apply(parse_update(line, line_number));
            switch (result.status) {
            case UpdateStatus::Accepted:
                ++accepted;
                break;
            case UpdateStatus::Duplicate:
                ++duplicates;
                break;
            case UpdateStatus::Stale:
                ++stale;
                break;
            case UpdateStatus::SequenceGap:
                ++gaps;
                break;
            case UpdateStatus::InvalidSide:
                ++invalid;
                break;
            }
        }

        std::cout << std::setprecision(12);
        std::cout << "events processed: " << processed << '\n';
        std::cout << "events accepted: " << accepted << '\n';
        std::cout << "duplicate events: " << duplicates << '\n';
        std::cout << "stale events: " << stale << '\n';
        std::cout << "sequence gaps: " << gaps << '\n';
        std::cout << "invalid events: " << invalid << '\n';
        print_level("best bid", book.best_bid());
        print_level("best ask", book.best_ask());
        print_optional("spread", book.spread());
        print_optional("mid", book.mid_price());
        print_optional("microprice", book.microprice());
    } catch (const std::exception& error) {
        std::cerr << "replay failed: " << error.what() << '\n';
        return 1;
    }

    return 0;
}
