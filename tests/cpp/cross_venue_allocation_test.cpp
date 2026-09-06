#include "fairvaluelab/cross_venue.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <new>

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

void* operator new[](const std::size_t size) { return ::operator new(size); }

void operator delete(void* pointer) noexcept {
    deallocation_count.fetch_add(1, std::memory_order_relaxed);
    std::free(pointer);
}

void operator delete[](void* pointer) noexcept { ::operator delete(pointer); }
void operator delete(void* pointer, const std::size_t) noexcept { ::operator delete(pointer); }
void operator delete[](void* pointer, const std::size_t) noexcept { ::operator delete(pointer); }

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

int main() {
    using namespace fairvaluelab;
    constexpr std::array venues{VenueId{1}, VenueId{2}, VenueId{3}};
    constexpr std::array pairs{VenuePair{1, 2}, VenuePair{1, 3}, VenuePair{2, 3}};
    CrossVenueSynchronizer synchronizer{
        venues, pairs, CrossVenueSynchronizerConfig{.max_staleness_ns = 1'000'000}};
    std::array<VenueCrossFeatures, 3> venue_output{};
    std::array<PairwiseCrossFeatures, 3> pair_output{};
    FeatureSet feature;
    feature.sample_kind = SampleKind::Event;
    feature.spread_ticks = 2;
    feature.best_bid_ticks = 100;
    feature.best_ask_ticks = 102;
    feature.mid_price = 101.0;
    feature.microprice = 101.0;
    feature.imbalance_l1 = 0.0;
    feature.imbalance_l3 = 0.0;
    feature.imbalance_l5 = 0.0;
    for (const auto venue_id : venues) {
        feature.venue_id = venue_id;
        feature.exchange_timestamp_ns = 90;
        feature.local_receipt_timestamp_ns = 100;
        feature.sample_timestamp_ns = 100;
        if (synchronizer.update(feature) != SynchronizerUpdateStatus::Accepted) {
            return 1;
        }
    }

    constexpr std::size_t event_count = 100'000;
    double checksum = 0.0;
    allocation_count.store(0, std::memory_order_relaxed);
    deallocation_count.store(0, std::memory_order_relaxed);
    for (std::size_t index = 0; index < event_count; ++index) {
        const auto timestamp = static_cast<TimestampNs>(101 + index);
        feature.venue_id = venues[index % venues.size()];
        feature.exchange_timestamp_ns = timestamp - 1;
        feature.local_receipt_timestamp_ns = timestamp;
        feature.sample_timestamp_ns = timestamp;
        feature.mid_price = 101.0 + static_cast<double>(index % 3);
        feature.microprice = *feature.mid_price + 0.25;
        feature.ofi_event_window = static_cast<double>(index % 17);
        feature.ofi_time_window = static_cast<double>(index % 11);
        if (synchronizer.update(feature) != SynchronizerUpdateStatus::Accepted ||
            !synchronizer.venue_features(timestamp, venue_output) ||
            !synchronizer.pairwise_features(timestamp, pair_output)) {
            return 1;
        }
        const auto consolidated = synchronizer.consolidated_reference(timestamp);
        checksum += consolidated.mid.value_or(0.0);
        checksum += pair_output[0].mid_difference.value_or(0.0);
    }
    const auto allocations = allocation_count.load(std::memory_order_relaxed);
    const auto deallocations = deallocation_count.load(std::memory_order_relaxed);
    if (allocations != 0 || deallocations != 0 || checksum == 0.0) {
        std::cerr << "allocations=" << allocations << " deallocations=" << deallocations
                  << " checksum=" << checksum << '\n';
        return 1;
    }
    std::cout << event_count << " synchronized updates, zero allocations and deallocations\n";
    return 0;
}
