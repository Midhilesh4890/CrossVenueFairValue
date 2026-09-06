#include "fairvaluelab/research_sampler.hpp"

#include <limits>
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

void fairvaluelab::align_future_targets(const std::span<CrossVenueSample> samples,
                                        const std::span<const TimestampNs> horizons_ns) {
    for (std::size_t index = 1; index < samples.size(); ++index) {
        if (samples[index].sample_timestamp_ns < samples[index - 1].sample_timestamp_ns) {
            throw std::invalid_argument("samples must be ordered by timestamp");
        }
    }
    for (std::size_t index = 0; index < horizons_ns.size(); ++index) {
        if (horizons_ns[index] == 0) {
            throw std::invalid_argument("target horizons must be positive");
        }
        for (std::size_t prior = 0; prior < index; ++prior) {
            if (horizons_ns[prior] == horizons_ns[index]) {
                throw std::invalid_argument("target horizons must be unique");
            }
        }
    }

    for (auto& sample : samples) {
        sample.future_targets.clear();
        sample.future_targets.reserve(horizons_ns.size());
        for (const auto horizon : horizons_ns) {
            FutureFairValueTarget target{};
            target.horizon_ns = horizon;
            sample.future_targets.push_back(target);
        }
    }

    for (std::size_t horizon_index = 0; horizon_index < horizons_ns.size(); ++horizon_index) {
        std::size_t target_index = 0;
        for (std::size_t sample_index = 0; sample_index < samples.size(); ++sample_index) {
            const auto sample_timestamp = samples[sample_index].sample_timestamp_ns;
            const auto horizon = horizons_ns[horizon_index];
            if (sample_timestamp > std::numeric_limits<TimestampNs>::max() - horizon) {
                continue;
            }
            const auto target_threshold = sample_timestamp + horizon;
            if (target_index <= sample_index) {
                target_index = sample_index + 1;
            }
            while (target_index < samples.size() &&
                   (samples[target_index].sample_timestamp_ns < target_threshold ||
                    (samples[target_index].consolidated.valid_venue_count == 0 ||
                     (!samples[target_index].consolidated.mid.has_value() &&
                      !samples[target_index].consolidated.microprice.has_value())))) {
                ++target_index;
            }
            if (target_index == samples.size()) {
                continue;
            }

            auto& target = samples[sample_index].future_targets[horizon_index];
            const auto& future = samples[target_index];
            target.target_timestamp_ns = future.sample_timestamp_ns;
            target.target_delay_ns = future.sample_timestamp_ns - target_threshold;
            target.future_consolidated_mid = future.consolidated.mid;
            target.future_consolidated_microprice = future.consolidated.microprice;
            if (target.future_consolidated_mid.has_value() &&
                samples[sample_index].consolidated.mid.has_value()) {
                target.mid_return = *target.future_consolidated_mid -
                                    *samples[sample_index].consolidated.mid;
                target.mid_direction = *target.mid_return > 0.0   ? 1
                                       : *target.mid_return < 0.0 ? -1
                                                                  : 0;
            }
            if (target.future_consolidated_microprice.has_value() &&
                samples[sample_index].consolidated.microprice.has_value()) {
                target.microprice_return = *target.future_consolidated_microprice -
                                           *samples[sample_index].consolidated.microprice;
                target.microprice_direction = *target.microprice_return > 0.0   ? 1
                                              : *target.microprice_return < 0.0 ? -1
                                                                                 : 0;
            }
        }
    }
}
