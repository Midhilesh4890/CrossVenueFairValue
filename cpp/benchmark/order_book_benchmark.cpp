#include "fairvaluelab/order_book.hpp"

#include <charconv>
#include <chrono>
#include <cstdint>
#include <exception>
#include <iomanip>
#include <iostream>
#include <limits>
#include <random>
#include <string_view>
#include <vector>

namespace {

constexpr std::uint64_t default_event_count = 1'000'000;
constexpr std::uint64_t random_seed = 0xF41A'B00C'2020ULL;

bool parse_event_count(const std::string_view value, std::uint64_t& event_count) {
    const auto [end, error] =
        std::from_chars(value.data(), value.data() + value.size(), event_count);
    return error == std::errc{} && end == value.data() + value.size() && event_count != 0;
}

} // namespace

int main(const int argc, const char* const argv[]) {
    std::uint64_t event_count = default_event_count;
    if (argc == 3 && std::string_view{argv[1]} == "--events") {
        if (!parse_event_count(argv[2], event_count)) {
            std::cerr << "--events requires a positive integer\n";
            return 1;
        }
    } else if (argc != 1) {
        std::cerr << "usage: fvl_order_book_benchmark [--events COUNT]\n";
        return 1;
    }

    if (event_count > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
        std::cerr << "event count exceeds this platform's addressable range\n";
        return 1;
    }

    try {
        std::vector<fairvaluelab::BookUpdate> events;
        events.reserve(static_cast<std::size_t>(event_count));
        std::mt19937_64 random{random_seed};
        constexpr fairvaluelab::PriceTicks reference_price = 1'000'000;

        for (std::uint64_t index = 0; index < event_count; ++index) {
            const auto side =
                (random() & 1U) == 0U ? fairvaluelab::Side::Bid : fairvaluelab::Side::Ask;
            const auto distance = static_cast<fairvaluelab::PriceTicks>(random() % 96U);
            const auto price = side == fairvaluelab::Side::Bid ? reference_price - distance
                                                               : reference_price + 1 + distance;
            const auto quantity = (random() % 20U) == 0U ? 0U : 1U + random() % 1'000U;
            events.push_back(fairvaluelab::BookUpdate{
                side,
                price,
                quantity,
                index * 100U,
                index * 100U + 25U,
                index + 1U,
            });
        }

        fairvaluelab::OrderBook book;
        std::uint64_t accepted = 0;
        const auto start = std::chrono::steady_clock::now();
        for (const auto& event : events) {
            accepted += static_cast<std::uint64_t>(book.apply(event).accepted());
        }
        const auto finish = std::chrono::steady_clock::now();

        const auto elapsed_ns =
            std::chrono::duration_cast<std::chrono::nanoseconds>(finish - start).count();
        if (elapsed_ns <= 0) {
            std::cerr << "benchmark duration was below the clock resolution\n";
            return 1;
        }
        const auto elapsed_seconds = static_cast<double>(elapsed_ns) / 1'000'000'000.0;
        const auto updates_per_second = static_cast<double>(event_count) / elapsed_seconds;
        const auto average_ns = static_cast<double>(elapsed_ns) / static_cast<double>(event_count);

        std::uint64_t checksum = accepted;
        if (const auto bid = book.best_bid(); bid.has_value()) {
            checksum ^= static_cast<std::uint64_t>(bid->price_ticks) ^ bid->quantity;
        }
        if (const auto ask = book.best_ask(); ask.has_value()) {
            checksum ^= static_cast<std::uint64_t>(ask->price_ticks) ^ (ask->quantity << 1U);
        }
        if (const auto sequence = book.last_sequence_number(); sequence.has_value()) {
            checksum ^= *sequence;
        }

        std::cout << std::fixed << std::setprecision(3);
        std::cout << "total events: " << event_count << '\n';
        std::cout << "accepted events: " << accepted << '\n';
        std::cout << "total elapsed time (ns): " << elapsed_ns << '\n';
        std::cout << "updates per second: " << updates_per_second << '\n';
        std::cout << "average nanoseconds per update: " << average_ns << '\n';
        std::cout << "result checksum: " << checksum << '\n';
    } catch (const std::exception& error) {
        std::cerr << "benchmark failed: " << error.what() << '\n';
        return 1;
    }

    return 0;
}
