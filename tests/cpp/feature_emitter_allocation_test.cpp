#include "fairvaluelab/feature_emitter.hpp"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <new>
#include <string_view>
#include <vector>
#include <utility>

#if defined(_WIN32)
#include <malloc.h>
#endif

std::atomic<std::size_t> allocation_count{};
std::atomic<std::size_t> deallocation_count{};

void* operator new(const std::size_t size) {
    allocation_count.fetch_add(1, std::memory_order_relaxed);
    if (void* pointer = std::malloc(size == 0 ? 1 : size)) {
        return pointer;
    }
    throw std::bad_alloc{};
}

void* operator new[](const std::size_t size) {
    allocation_count.fetch_add(1, std::memory_order_relaxed);
    if (void* pointer = std::malloc(size == 0 ? 1 : size)) {
        return pointer;
    }
    throw std::bad_alloc{};
}

void operator delete(void* pointer) noexcept {
    deallocation_count.fetch_add(1, std::memory_order_relaxed);
    std::free(pointer);
}

void operator delete[](void* pointer) noexcept {
    deallocation_count.fetch_add(1, std::memory_order_relaxed);
    std::free(pointer);
}

void operator delete(void* pointer, const std::size_t) noexcept {
    deallocation_count.fetch_add(1, std::memory_order_relaxed);
    std::free(pointer);
}

void operator delete[](void* pointer, const std::size_t) noexcept {
    deallocation_count.fetch_add(1, std::memory_order_relaxed);
    std::free(pointer);
}

void* operator new(const std::size_t size, const std::align_val_t alignment) {
    allocation_count.fetch_add(1, std::memory_order_relaxed);
    void* pointer = nullptr;
#if defined(_WIN32)
    pointer = _aligned_malloc(size == 0 ? 1 : size, static_cast<std::size_t>(alignment));
#else
    if (posix_memalign(&pointer, static_cast<std::size_t>(alignment), size == 0 ? 1 : size) != 0) {
        pointer = nullptr;
    }
#endif
    if (pointer != nullptr) {
        return pointer;
    }
    throw std::bad_alloc{};
}

void* operator new[](const std::size_t size, const std::align_val_t alignment) {
    return ::operator new(size, alignment);
}

void operator delete(void* pointer, const std::align_val_t) noexcept {
    deallocation_count.fetch_add(1, std::memory_order_relaxed);
#if defined(_WIN32)
    _aligned_free(pointer);
#else
    std::free(pointer);
#endif
}

void operator delete[](void* pointer, const std::align_val_t alignment) noexcept {
    ::operator delete(pointer, alignment);
}

void operator delete(void* pointer, const std::size_t, const std::align_val_t alignment) noexcept {
    ::operator delete(pointer, alignment);
}

void operator delete[](void* pointer, const std::size_t, const std::align_val_t alignment) noexcept {
    ::operator delete(pointer, alignment);
}

#define FVL_CHECK(condition) \
    do { \
        if (!(condition)) { \
            std::cerr << __FILE__ << ':' << __LINE__ << ": check failed: " #condition << '\n'; \
            return false; \
        } \
    } while (false)

bool test_accepted_update_path() {
    using namespace fairvaluelab;
    for (const auto capacity : {std::size_t{1}, std::size_t{100},
                                FeatureEmitter::maximum_event_window}) {
        FeatureEmitter emitter{FeatureEmitterConfig{
            .clock_interval_ns = 10'000,
            .event_window = capacity,
            .time_window_ns = 1'000'000'000,
            .band_ticks = 5,
            .venue_capacity = 2,
        }};
        std::vector<FeatureSet> output;
        output.reserve(16);
        constexpr std::uint64_t phase_size = 5'000;
        std::uint64_t updates_fed = 0;
        std::uint64_t accepted_updates = 0;
        std::uint64_t trades_fed = 0;
        std::uint64_t accepted_trades = 0;
        std::uint64_t event_only_updates = 0;
        std::uint64_t clock_updates = 0;
        std::uint64_t clock_samples = 0;
        bool samples_correct = true;
        TimestampNs timestamp = 0;
        allocation_count.store(0, std::memory_order_relaxed);
        deallocation_count.store(0, std::memory_order_relaxed);

        emitter.process(Trade{9, TradeSide::Buy, 101, 1, 0, 0, 1}, output);
        ++trades_fed;
        accepted_trades += output.size() == 1 && output.back().sample_kind == SampleKind::Event;
        output.clear();
        for (SequenceNumber sequence = 1; sequence <= 2 * phase_size; ++sequence) {
            timestamp += sequence <= phase_size ? 1 : 20'000;
            const auto side = sequence % 2 == 0 ? Side::Ask : Side::Bid;
            const auto result = emitter.process(BookUpdate{
                7, side, side == Side::Bid ? 100 : 102, 10 + sequence,
                timestamp, timestamp, sequence}, output);
            ++updates_fed;
            accepted_updates += result.accepted();
            std::size_t events = 0;
            std::size_t clocks = 0;
            for (const auto& sample : output) {
                events += sample.sample_kind == SampleKind::Event;
                clocks += sample.sample_kind == SampleKind::Clock;
            }
            samples_correct = samples_correct && events == 1 && !output.empty() &&
                output.back().venue_id == 7 && output.back().sample_kind == SampleKind::Event &&
                output.back().sample_timestamp_ns == timestamp;
            if (sequence <= phase_size) {
                event_only_updates += clocks == 0 && events == 1;
            } else {
                clock_updates += clocks == 4 && events == 1;
            }
            clock_samples += clocks;
            output.clear();
        }
        for (SequenceNumber sequence = 2; sequence <= phase_size + 1; ++sequence) {
            ++timestamp;
            const auto side = sequence % 2 == 0 ? TradeSide::Buy : TradeSide::Sell;
            emitter.process(Trade{7, side, side == TradeSide::Buy ? 102 : 100, sequence,
                                  timestamp, timestamp, sequence}, output);
            ++trades_fed;
            std::size_t events = 0;
            for (const auto& sample : output) {
                events += sample.sample_kind == SampleKind::Event;
            }
            accepted_trades += events == 1 && !output.empty() && output.back().venue_id == 7 &&
                output.back().sample_kind == SampleKind::Event &&
                output.back().sample_timestamp_ns == timestamp;
            samples_correct = samples_correct && !output.empty() &&
                output.back().trade_vwap_deviation_event_window.has_value() &&
                output.back().trade_count_event_window == std::min<std::uint64_t>(sequence - 1, capacity);
            output.clear();
        }
        const auto allocations = allocation_count.load(std::memory_order_relaxed);
        const auto deallocations = deallocation_count.load(std::memory_order_relaxed);
        FVL_CHECK(accepted_updates == updates_fed);
        FVL_CHECK(updates_fed == 2 * phase_size);
        FVL_CHECK(accepted_trades == trades_fed);
        FVL_CHECK(trades_fed == phase_size + 1);
        FVL_CHECK(event_only_updates == phase_size);
        FVL_CHECK(clock_updates == phase_size);
        FVL_CHECK(clock_samples == 4 * phase_size);
        FVL_CHECK(samples_correct);
        FVL_CHECK(allocations == 0);
        FVL_CHECK(deallocations == 0);
        FVL_CHECK(emitter.dropped_entries(7)->order_flow == updates_fed - capacity);
        FVL_CHECK(emitter.dropped_entries(7)->trade_flow == phase_size - capacity);
        FVL_CHECK(emitter.dropped_entries(9)->trade_flow == 0);
        std::cout << "capacity " << capacity << ": accepted " << accepted_updates << '/' << updates_fed
                  << " updates, " << accepted_trades << '/' << trades_fed
                  << " trades, " << clock_samples << " update clock samples, "
                  << allocations << " allocations, " << deallocations << " deallocations\n";
    }
    return true;
}

bool test_copied_emitter_path() {
    using namespace fairvaluelab;
    FeatureEmitter original{FeatureEmitterConfig{.venue_capacity = 2}};
    static_cast<void>(original.process(BookUpdate{7, Side::Bid, 100, 10, 0, 0, 1}));
    FeatureEmitter copied{original};
    FeatureEmitter assigned{FeatureEmitterConfig{.venue_capacity = 1}};
    assigned = original;
    FeatureEmitter moved{std::move(copied)};
    std::vector<FeatureSet> output;
    output.reserve(2);
    std::size_t accepted = 0;
    allocation_count.store(0, std::memory_order_relaxed);
    deallocation_count.store(0, std::memory_order_relaxed);
    accepted += moved.process(BookUpdate{9, Side::Bid, 100, 10, 1, 1, 1}, output).accepted();
    accepted += assigned.process(BookUpdate{9, Side::Bid, 100, 10, 1, 1, 1}, output).accepted();
    const auto allocations = allocation_count.load(std::memory_order_relaxed);
    const auto deallocations = deallocation_count.load(std::memory_order_relaxed);
    FVL_CHECK(accepted == 2);
    FVL_CHECK(output.size() == 2);
    FVL_CHECK(allocations == 0);
    FVL_CHECK(deallocations == 0);
    return true;
}

bool test_rejected_update_path() {
    fairvaluelab::FeatureEmitter emitter{fairvaluelab::FeatureEmitterConfig{
        .clock_interval_ns = 1'000'000'000,
        .event_window = 100,
        .time_window_ns = 1'000'000'000,
        .band_ticks = 5,
        .venue_capacity = 1,
    }};
    static_cast<void>(emitter.process(fairvaluelab::BookUpdate{
        7, fairvaluelab::Side::Bid, 100, 10, 0, 1, 1}));
    static_cast<void>(emitter.process(fairvaluelab::BookUpdate{
        7, fairvaluelab::Side::Ask, 102, 10, 1, 2, 2}));
    std::size_t rejected = 0;
    allocation_count.store(0, std::memory_order_relaxed);
    deallocation_count.store(0, std::memory_order_relaxed);
    for (std::uint64_t timestamp = 3; timestamp < 5'003; ++timestamp) {
        const auto output = emitter.process(fairvaluelab::BookUpdate{
            7, fairvaluelab::Side::Bid, 100, 10, timestamp - 1, timestamp,
            timestamp % 3 == 0 ? 2ULL : (timestamp % 3 == 1 ? 1ULL : 4ULL)});
        rejected += output.empty();
    }
    const auto allocations = allocation_count.load(std::memory_order_relaxed);
    const auto deallocations = deallocation_count.load(std::memory_order_relaxed);
    FVL_CHECK(rejected == 5'000);
    FVL_CHECK(allocations == 0);
    FVL_CHECK(deallocations == 0);
    return true;
}

int main(const int argc, const char* const argv[]) {
    if (argc != 2) {
        return 1;
    }
    const std::string_view name{argv[1]};
    if (name == "accepted") {
        return test_accepted_update_path() && test_copied_emitter_path() ? 0 : 1;
    }
    if (name == "rejected") {
        return test_rejected_update_path() ? 0 : 1;
    }
    return 1;
}
