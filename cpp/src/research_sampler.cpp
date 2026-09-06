#include "fairvaluelab/research_sampler.hpp"

#include <stdexcept>

fairvaluelab::ResearchSampler::ResearchSampler(const std::span<const VenueId> venue_ids,
                                               const std::span<const VenuePair> venue_pairs,
                                               ResearchSamplerConfig config)
    : config_(config), emitter_(config.feature_emitter),
      synchronizer_(venue_ids, venue_pairs, config.synchronizer) {
    if (config_.sample_kind != SampleKind::Clock && config_.sample_kind != SampleKind::Event) {
        throw std::invalid_argument("unsupported research sample kind");
    }
    emitted_features_.reserve(venue_ids.size() + 1);
}

fairvaluelab::ApplyResult
fairvaluelab::ResearchSampler::process(const BookUpdate& update,
                                      std::vector<CrossVenueSample>& output) {
    emitted_features_.clear();
    const auto result = emitter_.process(update, emitted_features_);
    consume_emitted_features(output);
    return result;
}

void fairvaluelab::ResearchSampler::process(const Trade& trade,
                                            std::vector<CrossVenueSample>& output) {
    emitted_features_.clear();
    emitter_.process(trade, emitted_features_);
    consume_emitted_features(output);
}

fairvaluelab::SampleKind fairvaluelab::ResearchSampler::sample_kind() const noexcept {
    return config_.sample_kind;
}

void fairvaluelab::ResearchSampler::consume_emitted_features(
    std::vector<CrossVenueSample>& output) {
    std::optional<TimestampNs> pending_clock_timestamp;
    for (const auto& features : emitted_features_) {
        if (pending_clock_timestamp.has_value() &&
            (features.sample_kind != SampleKind::Clock ||
             features.sample_timestamp_ns != *pending_clock_timestamp)) {
            append_sample(*pending_clock_timestamp, output);
            pending_clock_timestamp.reset();
        }
        const auto status = synchronizer_.update(features);
        if (status != SynchronizerUpdateStatus::Accepted) {
            throw std::runtime_error("feature emitter produced invalid synchronization order");
        }
        if (config_.sample_kind == SampleKind::Event && features.sample_kind == SampleKind::Event) {
            append_sample(features.sample_timestamp_ns, output);
        } else if (config_.sample_kind == SampleKind::Clock &&
                   features.sample_kind == SampleKind::Clock) {
            pending_clock_timestamp = features.sample_timestamp_ns;
        }
    }
    if (pending_clock_timestamp.has_value()) {
        append_sample(*pending_clock_timestamp, output);
    }
}

void fairvaluelab::ResearchSampler::append_sample(const TimestampNs sample_timestamp_ns,
                                                  std::vector<CrossVenueSample>& output) {
    CrossVenueSample sample;
    sample.sample_kind = config_.sample_kind;
    sample.sample_timestamp_ns = sample_timestamp_ns;
    sample.consolidated = synchronizer_.consolidated_reference(sample_timestamp_ns);
    sample.venue_features.resize(synchronizer_.venue_count());
    sample.pairwise_features.resize(synchronizer_.pair_count());
    if (!synchronizer_.venue_features(sample_timestamp_ns, sample.venue_features) ||
        !synchronizer_.pairwise_features(sample_timestamp_ns, sample.pairwise_features)) {
        throw std::runtime_error("invalid synchronized sample storage");
    }
    output.push_back(std::move(sample));
}
