#include "fairvaluelab/feature_emitter.hpp"

#include <atomic>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <new>

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

int main() {
    fairvaluelab::FeatureEmitter emitter{fairvaluelab::FeatureEmitterConfig{
        .clock_interval_ns = 1'000'000'000,
        .event_window = 100,
        .time_window_ns = 1'000'000'000,
        .multi_level_depth = 5,
    }};
    static_cast<void>(emitter.process(fairvaluelab::BookUpdate{7, fairvaluelab::Side::Bid, 100,
                                                               10, 0, 1, 1}));

    // Warm both sides. Only rejected updates without clock emissions are
    // allocation-free; accepted updates still allocate output/history storage.
    static_cast<void>(emitter.process(fairvaluelab::BookUpdate{
        7, fairvaluelab::Side::Ask, 102, 10, 1, 2, 2}));
    allocation_count.store(0, std::memory_order_relaxed);
    deallocation_count.store(0, std::memory_order_relaxed);
    for (std::uint64_t timestamp = 3; timestamp < 5'003; ++timestamp) {
        static_cast<void>(emitter.process(fairvaluelab::BookUpdate{
            7, fairvaluelab::Side::Bid, 100, 10, timestamp - 1, timestamp,
            timestamp % 3 == 0 ? 2ULL : (timestamp % 3 == 1 ? 1ULL : 4ULL)}));
    }
    if (allocation_count.load(std::memory_order_relaxed) != 0 ||
        deallocation_count.load(std::memory_order_relaxed) != 0) {
        std::cerr << "rejected update processing allocated or deallocated memory\n";
        return 1;
    }
    return 0;
}
