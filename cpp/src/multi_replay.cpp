#include "fairvaluelab/multi_replay.hpp"

#include "fairvaluelab/order_book.hpp"

#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <istream>
#include <map>
#include <optional>
#include <ostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

using fairvaluelab::BookUpdate;
using fairvaluelab::OrderBook;
using fairvaluelab::PriceLevel;
using fairvaluelab::ReplayReport;
using fairvaluelab::Side;
using fairvaluelab::UpdateStatus;
using fairvaluelab::VenueId;

template <typename Integer> Integer parse_integer(const std::string_view value) {
    Integer parsed{};
    const auto [end, error] =
        std::from_chars(value.data(), value.data() + value.size(), parsed);
    if (error != std::errc{} || end != value.data() + value.size()) {
        throw std::runtime_error("invalid integer");
    }
    return parsed;
}

std::array<std::string_view, 7> split_row(const std::string& line) {
    std::array<std::string_view, 7> columns{};
    const std::string_view row{line};
    std::size_t begin = 0;
    for (std::size_t column = 0; column < columns.size(); ++column) {
        const auto separator = row.find(',', begin);
        const bool is_last = column + 1 == columns.size();
        if ((separator == std::string_view::npos) != is_last) {
            throw std::runtime_error("expected seven columns");
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

BookUpdate parse_event(const std::array<std::string_view, 7>& columns) {
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

std::optional<VenueId> find_venue_id(const std::string_view line) {
    const auto first = line.find(',');
    if (first == std::string_view::npos) {
        return std::nullopt;
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

void print_level(std::ostream& output, const std::optional<PriceLevel> level) {
    if (!level.has_value()) {
        output << "unavailable";
        return;
    }
    output << level->price_ticks << ':' << level->quantity;
}

void print_snapshot(std::ostream& output, const std::uint64_t event_count,
                    const std::map<VenueId, OrderBook>& books) {
    for (const auto& [venue_id, book] : books) {
        output << "event " << event_count << " venue " << venue_id << " bid=";
        print_level(output, book.best_bid());
        output << " ask=";
        print_level(output, book.best_ask());
        output << '\n';
    }
}

}

fairvaluelab::ReplayReport fairvaluelab::replay_normalized_log(std::istream& input,
                                                               const std::size_t snapshot_interval,
                                                               std::ostream* snapshots) {
    if (snapshot_interval == 0) {
        throw std::invalid_argument("snapshot interval must be positive");
    }

    std::string line;
    if (!std::getline(input, line)) {
        throw std::runtime_error("normalized log is empty");
    }
    if (line != "sequence_number,venue_id,exchange_timestamp_ns,local_receipt_timestamp_ns,side,"
                "price_ticks,quantity" &&
        line != "sequence_number,venue_id,exchange_timestamp_ns,local_receipt_timestamp_ns,side,"
                "price_ticks,quantity\r") {
        throw std::runtime_error("invalid normalized log header");
    }

    ReplayReport report;
    std::map<VenueId, OrderBook> books;
    std::uint64_t event_count = 0;
    while (std::getline(input, line)) {
        if (line.empty()) {
            continue;
        }
        ++event_count;
        try {
            const auto event = parse_event(split_row(line));
            auto& stats = report[event.venue_id];
            auto [book, inserted] = books.try_emplace(event.venue_id);
            static_cast<void>(inserted);
            switch (book->second.apply(event).status) {
            case UpdateStatus::Accepted:
                ++stats.accepted;
                break;
            case UpdateStatus::Duplicate:
                ++stats.duplicate;
                break;
            case UpdateStatus::Stale:
                ++stats.stale;
                break;
            case UpdateStatus::SequenceGap:
                ++stats.gapped;
                break;
            case UpdateStatus::InvalidSide:
                ++stats.malformed;
                break;
            }
        } catch (const std::exception&) {
            const auto venue_id = find_venue_id(line);
            if (venue_id.has_value()) {
                ++report[*venue_id].malformed;
            }
        }

        if (snapshots != nullptr && event_count % snapshot_interval == 0) {
            print_snapshot(*snapshots, event_count, books);
        }
    }

    if (snapshots != nullptr && event_count % snapshot_interval != 0) {
        print_snapshot(*snapshots, event_count, books);
    }
    return report;
}
