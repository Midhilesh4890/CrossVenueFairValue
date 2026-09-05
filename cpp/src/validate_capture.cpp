#include "fairvaluelab/capture_validation.hpp"
#include "fairvaluelab/venue_adapters.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace {

using fairvaluelab::AdapterStatus;
using fairvaluelab::BinanceAdapter;
using fairvaluelab::BookUpdate;
using fairvaluelab::CaptureChecksumStatus;
using fairvaluelab::CaptureContinuityStatus;
using fairvaluelab::CaptureSequenceState;
using fairvaluelab::CoinbaseAdapter;
using fairvaluelab::OkxAdapter;
using fairvaluelab::Rational;
using fairvaluelab::Trade;
using fairvaluelab::VenueAdapter;
using fairvaluelab::VenueConfig;

struct VenueReport {
    std::uint64_t accepted{};
    std::uint64_t malformed{};
    std::uint64_t unsupported{};
    std::uint64_t gaps{};
    std::uint64_t checksum_mismatches{};
    std::set<std::string> malformed_shape_set;
    std::vector<std::string> malformed_shapes;
    CaptureSequenceState sequence_state;
};

VenueConfig venue_config(const std::string& venue) {
    if (venue == "binance") {
        return VenueConfig{1, venue, Rational{1, 100}, 100'000'000, 64};
    }
    if (venue == "coinbase") {
        return VenueConfig{2, venue, Rational{1, 100}, 100'000'000, 64};
    }
    if (venue == "okx") {
        return VenueConfig{3, venue, Rational{1, 10}, 100'000'000, 64};
    }
    throw std::runtime_error("unsupported venue: " + venue);
}

std::unique_ptr<VenueAdapter> make_adapter(const std::string& venue) {
    const auto config = venue_config(venue);
    if (venue == "binance") {
        return std::make_unique<BinanceAdapter>(config);
    }
    if (venue == "coinbase") {
        return std::make_unique<CoinbaseAdapter>(config);
    }
    if (venue == "okx") {
        return std::make_unique<OkxAdapter>(config);
    }
    throw std::runtime_error("unsupported venue: " + venue);
}

CaptureContinuityStatus check_sequence(const std::string& venue, const std::string& line,
                                        CaptureSequenceState& state) {
    if (venue == "binance") {
        return fairvaluelab::check_binance_sequence(line, state);
    }
    if (venue == "coinbase") {
        return fairvaluelab::check_coinbase_sequence(line, state);
    }
    if (venue == "okx") {
        return fairvaluelab::check_okx_sequence(line, state);
    }
    return CaptureContinuityStatus::Unsupported;
}

bool is_gap(const CaptureContinuityStatus status) {
    return status == CaptureContinuityStatus::Gap || status == CaptureContinuityStatus::OutOfOrder;
}

void add_malformed_shape(VenueReport& report, const std::string& line) {
    const auto shape = fairvaluelab::derive_payload_shape(line);
    if (report.malformed_shape_set.insert(shape).second && report.malformed_shapes.size() < 20) {
        report.malformed_shapes.push_back(shape);
    }
}

void process_line(const std::string& venue, const std::string& line, VenueAdapter& adapter,
                  VenueReport& report) {
    std::vector<BookUpdate> updates;
    std::vector<Trade> trades;
    const auto book_status = adapter.normalize(line, updates);
    AdapterStatus status = book_status;
    if (book_status != AdapterStatus::Accepted) {
        const auto trade_status = adapter.normalize_trades(line, trades);
        if (trade_status == AdapterStatus::Accepted) {
            status = AdapterStatus::Accepted;
        } else if (book_status == AdapterStatus::Malformed || trade_status == AdapterStatus::Malformed) {
            status = AdapterStatus::Malformed;
        } else {
            status = AdapterStatus::Unsupported;
        }
    }

    if (status == AdapterStatus::Accepted) {
        ++report.accepted;
    } else if (status == AdapterStatus::Malformed) {
        ++report.malformed;
        add_malformed_shape(report, line);
    } else {
        ++report.unsupported;
    }

    const auto continuity = check_sequence(venue, line, report.sequence_state);
    if (is_gap(continuity)) {
        ++report.gaps;
    }
    if (venue == "okx" && fairvaluelab::check_okx_checksum(line) == CaptureChecksumStatus::Mismatch) {
        ++report.checksum_mismatches;
    }
}

bool supported_capture_file(const std::filesystem::path& path) {
    if (path.extension() != ".ndjson") {
        return false;
    }
    const auto venue = path.stem().string();
    return venue == "binance" || venue == "coinbase" || venue == "okx";
}

}

int main(const int argc, const char* const argv[]) {
    if (argc != 2) {
        std::cerr << "usage: fvl_validate_capture <capture-directory>\n";
        return 1;
    }

    try {
        const std::filesystem::path capture_directory{argv[1]};
        if (!std::filesystem::is_directory(capture_directory)) {
            std::cerr << "capture directory does not exist: " << capture_directory << '\n';
            return 1;
        }

        std::map<std::string, VenueReport> reports;
        std::map<std::string, std::unique_ptr<VenueAdapter>> adapters;
        for (const auto& venue : {std::string{"binance"}, std::string{"coinbase"}, std::string{"okx"}}) {
            reports.emplace(venue, VenueReport{});
            adapters.emplace(venue, make_adapter(venue));
        }

        for (const auto& entry : std::filesystem::recursive_directory_iterator(capture_directory)) {
            if (!entry.is_regular_file() || !supported_capture_file(entry.path())) {
                continue;
            }
            const auto venue = entry.path().stem().string();
            std::ifstream input{entry.path()};
            if (!input) {
                throw std::runtime_error("failed to open " + entry.path().string());
            }
            std::string line;
            while (std::getline(input, line)) {
                if (!line.empty()) {
                    process_line(venue, line, *adapters.at(venue), reports.at(venue));
                }
            }
        }

        bool has_empty_accepted_venue = false;
        for (const auto& [venue, report] : reports) {
            std::cout << venue << ": accepted=" << report.accepted
                      << " malformed=" << report.malformed
                      << " unsupported=" << report.unsupported << " gaps=" << report.gaps
                      << " checksum_mismatches=" << report.checksum_mismatches << '\n';
            for (const auto& shape : report.malformed_shapes) {
                std::cout << venue << " malformed_shape=" << shape << '\n';
            }
            if (report.accepted == 0) {
                has_empty_accepted_venue = true;
            }
        }
        return has_empty_accepted_venue ? 2 : 0;
    } catch (const std::exception& error) {
        std::cerr << "capture validation failed: " << error.what() << '\n';
        return 1;
    }
}
