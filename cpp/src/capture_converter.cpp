#include "fairvaluelab/capture_converter.hpp"

#include "fairvaluelab/normalized_csv.hpp"
#include "fairvaluelab/venue.hpp"
#include "fairvaluelab/venue_adapter.hpp"
#include "fairvaluelab/venue_adapters.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace {

using fairvaluelab::AdapterStatus;
using fairvaluelab::BinanceAdapter;
using fairvaluelab::BookUpdate;
using fairvaluelab::CoinbaseAdapter;
using fairvaluelab::ConversionReport;
using fairvaluelab::OkxAdapter;
using fairvaluelab::NormalizedEvent;
using fairvaluelab::Rational;
using fairvaluelab::Side;
using fairvaluelab::VenueAdapter;
using fairvaluelab::VenueConfig;
using fairvaluelab::VenueId;
using fairvaluelab::Trade;
using fairvaluelab::TradeSide;

struct Source {
    VenueConfig config;
    std::string filename;
    std::unique_ptr<VenueAdapter> adapter;
};

template <typename Adapter> Source make_source(VenueConfig config, std::string filename) {
    auto adapter = std::make_unique<Adapter>(config);
    return Source{std::move(config), std::move(filename), std::move(adapter)};
}

void read_source(const std::filesystem::path& directory, const Source& source,
                 std::vector<NormalizedEvent>& events, ConversionReport& report) {
    const auto path = directory / source.filename;
    if (!std::filesystem::exists(path)) {
        return;
    }

    std::ifstream input{path};
    if (!input) {
        throw std::runtime_error("failed to open capture file: " + path.string());
    }

    auto& stats = report[source.config.venue_id];
    std::string line;
    while (std::getline(input, line)) {
        if (line.empty()) {
            continue;
        }
        std::vector<BookUpdate> normalized_events;
        switch (source.adapter->normalize(line, normalized_events)) {
        case AdapterStatus::Accepted:
            stats.accepted += normalized_events.size();
            for (const auto& event : normalized_events) {
                events.emplace_back(event);
            }
            break;
        case AdapterStatus::Malformed:
            ++stats.malformed;
            break;
        case AdapterStatus::Unsupported: {
            std::vector<Trade> trades;
            switch (source.adapter->normalize_trades(line, trades)) {
            case AdapterStatus::Accepted:
                stats.accepted += trades.size();
                for (const auto& trade : trades) {
                    events.emplace_back(trade);
                }
                break;
            case AdapterStatus::Malformed:
                ++stats.malformed;
                break;
            case AdapterStatus::Unsupported:
                ++stats.unsupported;
                break;
            }
            break;
        }
        }
    }
}

}

fairvaluelab::ConversionReport
fairvaluelab::convert_capture_directory(const std::filesystem::path& input,
                                        const std::filesystem::path& output) {
    if (!std::filesystem::is_directory(input)) {
        throw std::runtime_error("capture directory does not exist: " + input.string());
    }

    std::vector<Source> sources;
    sources.push_back(make_source<BinanceAdapter>(
        VenueConfig{1, "binance", Rational{1, 100}, 100'000'000, 64}, "binance.ndjson"));
    sources.push_back(make_source<CoinbaseAdapter>(
        VenueConfig{2, "coinbase", Rational{1, 100}, 100'000'000, 64}, "coinbase.ndjson"));
    sources.push_back(make_source<OkxAdapter>(
        VenueConfig{3, "okx", Rational{1, 10}, 100'000'000, 64}, "okx.ndjson"));

    std::vector<NormalizedEvent> events;
    ConversionReport report;
    for (const auto& source : sources) {
        read_source(input, source, events, report);
    }
    if (report.empty()) {
        throw std::runtime_error("capture directory contains no supported venue files");
    }

    std::stable_sort(events.begin(), events.end(), [](const NormalizedEvent& lhs,
                                                      const NormalizedEvent& rhs) {
        const auto lhs_timestamp = std::visit(
            [](const auto& event) { return event.local_receipt_timestamp_ns; }, lhs);
        const auto rhs_timestamp = std::visit(
            [](const auto& event) { return event.local_receipt_timestamp_ns; }, rhs);
        if (lhs_timestamp != rhs_timestamp) {
            return lhs_timestamp < rhs_timestamp;
        }
        const auto lhs_venue = std::visit([](const auto& event) { return event.venue_id; }, lhs);
        const auto rhs_venue = std::visit([](const auto& event) { return event.venue_id; }, rhs);
        if (lhs_venue != rhs_venue) {
            return lhs_venue < rhs_venue;
        }
        const auto lhs_sequence =
            std::visit([](const auto& event) { return event.sequence_number; }, lhs);
        const auto rhs_sequence =
            std::visit([](const auto& event) { return event.sequence_number; }, rhs);
        return lhs_sequence < rhs_sequence;
    });

    if (!output.parent_path().empty()) {
        std::filesystem::create_directories(output.parent_path());
    }
    std::ofstream normalized{output};
    if (!normalized) {
        throw std::runtime_error("failed to open normalized output: " + output.string());
    }
    normalized << normalized_csv_header << '\n';
    for (const auto& normalized_event : events) {
        std::visit(
            [&](const auto& event) {
                using Event = std::decay_t<decltype(event)>;
                normalized << (std::is_same_v<Event, BookUpdate> ? "book" : "trade") << ','
                           << event.sequence_number << ',' << event.venue_id << ','
                           << event.exchange_timestamp_ns << ','
                           << event.local_receipt_timestamp_ns << ',';
                if constexpr (std::is_same_v<Event, BookUpdate>) {
                    normalized << (event.side == Side::Bid ? 'B' : 'A');
                } else {
                    normalized << (event.side == TradeSide::Buy ? 'B' : 'S');
                }
                normalized << ',' << event.price_ticks << ',' << event.quantity << '\n';
            },
            normalized_event);
    }
    if (!normalized) {
        throw std::runtime_error("failed to write normalized output: " + output.string());
    }
    return report;
}
