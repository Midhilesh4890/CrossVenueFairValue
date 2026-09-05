#include "fairvaluelab/normalized_csv.hpp"

#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string_view>

namespace {

template <typename Integer> Integer parse_integer(const std::string_view value) {
    Integer parsed{};
    const auto [end, error] =
        std::from_chars(value.data(), value.data() + value.size(), parsed);
    if (error != std::errc{} || end != value.data() + value.size()) {
        throw std::runtime_error("invalid integer");
    }
    return parsed;
}

template <std::size_t ColumnCount>
std::array<std::string_view, ColumnCount> split_row(std::string_view row) {
    std::array<std::string_view, ColumnCount> columns{};
    std::size_t begin = 0;
    for (std::size_t column = 0; column < columns.size(); ++column) {
        const auto separator = row.find(',', begin);
        const bool is_last = column + 1 == columns.size();
        if ((separator == std::string_view::npos) != is_last) {
            throw std::runtime_error("unexpected column count");
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

}

bool fairvaluelab::is_normalized_csv_header(std::string_view line) noexcept {
    if (!line.empty() && line.back() == '\r') {
        line.remove_suffix(1);
    }
    return line == normalized_csv_header || line == legacy_normalized_csv_header;
}

fairvaluelab::NormalizedEvent
fairvaluelab::parse_normalized_event(const std::string_view line) {
    if (!line.starts_with("book,") && !line.starts_with("trade,")) {
        const auto columns = split_row<7>(line);
        Side side;
        if (columns[4] == "B") {
            side = Side::Bid;
        } else if (columns[4] == "A") {
            side = Side::Ask;
        } else {
            throw std::runtime_error("invalid side");
        }
        return BookUpdate{
            parse_integer<VenueId>(columns[1]),
            side,
            parse_integer<std::int64_t>(columns[5]),
            parse_integer<std::uint64_t>(columns[6]),
            parse_integer<std::uint64_t>(columns[2]),
            parse_integer<std::uint64_t>(columns[3]),
            parse_integer<std::uint64_t>(columns[0]),
        };
    }

    const auto columns = split_row<8>(line);
    if (columns[0] == "trade") {
        TradeSide side;
        if (columns[5] == "B") {
            side = TradeSide::Buy;
        } else if (columns[5] == "S") {
            side = TradeSide::Sell;
        } else {
            throw std::runtime_error("invalid trade side");
        }
        return Trade{
            parse_integer<VenueId>(columns[2]),
            side,
            parse_integer<std::int64_t>(columns[6]),
            parse_integer<std::uint64_t>(columns[7]),
            parse_integer<std::uint64_t>(columns[3]),
            parse_integer<std::uint64_t>(columns[4]),
            parse_integer<std::uint64_t>(columns[1]),
        };
    }

    Side side;
    if (columns[5] == "B") {
        side = Side::Bid;
    } else if (columns[5] == "A") {
        side = Side::Ask;
    } else {
        throw std::runtime_error("invalid side");
    }
    return BookUpdate{
        parse_integer<VenueId>(columns[2]),
        side,
        parse_integer<std::int64_t>(columns[6]),
        parse_integer<std::uint64_t>(columns[7]),
        parse_integer<std::uint64_t>(columns[3]),
        parse_integer<std::uint64_t>(columns[4]),
        parse_integer<std::uint64_t>(columns[1]),
    };
}

std::optional<fairvaluelab::VenueId>
fairvaluelab::find_normalized_venue_id(const std::string_view line) noexcept {
    auto first = line.find(',');
    if (first == std::string_view::npos) {
        return std::nullopt;
    }
    if (line.starts_with("book,") || line.starts_with("trade,")) {
        first = line.find(',', first + 1);
        if (first == std::string_view::npos) {
            return std::nullopt;
        }
    }
    const auto second = line.find(',', first + 1);
    if (second == std::string_view::npos) {
        return std::nullopt;
    }
    try {
        return parse_integer<VenueId>(line.substr(first + 1, second - first - 1));
    } catch (const std::exception&) {
        return std::nullopt;
    }
}
