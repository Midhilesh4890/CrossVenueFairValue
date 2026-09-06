#pragma once

#include "fairvaluelab/cross_venue.hpp"

#include <span>
#include <vector>

namespace fairvaluelab {

struct CrossVenueSample {
    SampleKind sample_kind{SampleKind::Clock};
    TimestampNs sample_timestamp_ns{};
    ConsolidatedReference consolidated;
    std::vector<VenueCrossFeatures> venue_features;
    std::vector<PairwiseCrossFeatures> pairwise_features;
};

struct ResearchSamplerConfig {
    SampleKind sample_kind{SampleKind::Clock};
    FeatureEmitterConfig feature_emitter;
    CrossVenueSynchronizerConfig synchronizer;
};

class ResearchSampler {
  public:
    ResearchSampler(std::span<const VenueId> venue_ids, std::span<const VenuePair> venue_pairs,
                    ResearchSamplerConfig config = ResearchSamplerConfig{});

    [[nodiscard]] ApplyResult process(const BookUpdate& update,
                                      std::vector<CrossVenueSample>& output);
    void process(const Trade& trade, std::vector<CrossVenueSample>& output);
    [[nodiscard]] SampleKind sample_kind() const noexcept;

  private:
    void consume_emitted_features(std::vector<CrossVenueSample>& output);
    void append_sample(TimestampNs sample_timestamp_ns, std::vector<CrossVenueSample>& output);

    ResearchSamplerConfig config_;
    FeatureEmitter emitter_;
    CrossVenueSynchronizer synchronizer_;
    std::vector<FeatureSet> emitted_features_;
};

} // namespace fairvaluelab
