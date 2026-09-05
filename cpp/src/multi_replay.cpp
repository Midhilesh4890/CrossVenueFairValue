#include "fairvaluelab/multi_replay.hpp"

#include "fairvaluelab/normalized_csv.hpp"
#include "fairvaluelab/order_book.hpp"

#include <cstddef>
#include <cstdint>
#include <istream>
#include <map>
#include <optional>
#include <ostream>
#include <stdexcept>
#include <string>

namespace {

using fairvaluelab::OrderBook;
using fairvaluelab::PriceLevel;
using fairvaluelab::ReplayReport;
using fairvaluelab::Side;
using fairvaluelab::UpdateStatus;
using fairvaluelab::VenueId;

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
    if (!is_normalized_csv_header(line)) {
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
            const auto event = parse_normalized_event(line);
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
            const auto venue_id = find_normalized_venue_id(line);
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
