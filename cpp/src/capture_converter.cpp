#include "fairvaluelab/capture_converter.hpp"

#include "fairvaluelab/venue.hpp"
#include "fairvaluelab/venue_adapter.hpp"
#include "fairvaluelab/venue_adapters.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using fairvaluelab::AdapterStatus;
using fairvaluelab::BinanceAdapter;
using fairvaluelab::BookUpdate;
using fairvaluelab::CoinbaseAdapter;
using fairvaluelab::ConversionReport;
using fairvaluelab::OkxAdapter;
using fairvaluelab::Rational;
using fairvaluelab::Side;
using fairvaluelab::VenueAdapter;
using fairvaluelab::VenueConfig;
using fairvaluelab::VenueId;

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
                 std::vector<BookUpdate>& events, ConversionReport& report) {
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
            events.insert(events.end(), normalized_events.begin(), normalized_events.end());
            break;
        case AdapterStatus::Malformed:
            ++stats.malformed;
            break;
        case AdapterStatus::Unsupported:
            ++stats.unsupported;
            break;
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

    std::vector<BookUpdate> events;
    ConversionReport report;
    for (const auto& source : sources) {
        read_source(input, source, events, report);
    }
    if (report.empty()) {
        throw std::runtime_error("capture directory contains no supported venue files");
    }

    std::stable_sort(events.begin(), events.end(), [](const BookUpdate& lhs, const BookUpdate& rhs) {
        if (lhs.local_receipt_timestamp_ns != rhs.local_receipt_timestamp_ns) {
            return lhs.local_receipt_timestamp_ns < rhs.local_receipt_timestamp_ns;
        }
        if (lhs.venue_id != rhs.venue_id) {
            return lhs.venue_id < rhs.venue_id;
        }
        return lhs.sequence_number < rhs.sequence_number;
    });

    if (!output.parent_path().empty()) {
        std::filesystem::create_directories(output.parent_path());
    }
    std::ofstream normalized{output};
    if (!normalized) {
        throw std::runtime_error("failed to open normalized output: " + output.string());
    }
    normalized << "sequence_number,venue_id,exchange_timestamp_ns,local_receipt_timestamp_ns,side,"
                  "price_ticks,quantity\n";
    for (const auto& event : events) {
        normalized << event.sequence_number << ',' << event.venue_id << ','
                   << event.exchange_timestamp_ns << ',' << event.local_receipt_timestamp_ns << ','
                   << (event.side == Side::Bid ? 'B' : 'A') << ',' << event.price_ticks << ','
                   << event.quantity << '\n';
    }
    if (!normalized) {
        throw std::runtime_error("failed to write normalized output: " + output.string());
    }
    return report;
}
