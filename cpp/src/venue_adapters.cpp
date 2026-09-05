#include "fairvaluelab/venue_adapters.hpp"

#include <nlohmann/json.hpp>

#include <charconv>
#include <chrono>
#include <cstdint>
#include <limits>
#include <numeric>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using fairvaluelab::AdapterStatus;
using fairvaluelab::BookUpdate;
using fairvaluelab::Quantity;
using fairvaluelab::Rational;
using fairvaluelab::Side;
using fairvaluelab::TimestampNs;
using fairvaluelab::Trade;
using fairvaluelab::TradeSide;
using fairvaluelab::VenueConfig;
using Json = nlohmann::json;

struct Envelope {
    TimestampNs local_receipt_timestamp_ns{};
    Json payload;
};

enum class SequenceStatus : std::uint8_t {
    Accepted,
    Duplicate,
    Stale,
    Gap,
};

template <typename Integer> std::optional<Integer> parse_integer(const std::string_view value) {
    Integer output{};
    const auto [end, error] =
        std::from_chars(value.data(), value.data() + value.size(), output);
    if (error != std::errc{} || end != value.data() + value.size()) {
        return std::nullopt;
    }
    return output;
}

std::optional<Rational> parse_decimal(const std::string_view value) {
    if (value.empty()) {
        return std::nullopt;
    }

    std::size_t index = 0;
    bool negative = false;
    if (value.front() == '-' || value.front() == '+') {
        negative = value.front() == '-';
        index = 1;
    }
    if (index == value.size()) {
        return std::nullopt;
    }

    std::uint64_t magnitude = 0;
    std::uint64_t denominator = 1;
    bool decimal_point = false;
    bool has_digit = false;
    constexpr auto maximum = static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
    for (; index < value.size(); ++index) {
        const char character = value[index];
        if (character == '.' && !decimal_point) {
            decimal_point = true;
            continue;
        }
        if (character < '0' || character > '9') {
            return std::nullopt;
        }
        const auto digit = static_cast<std::uint64_t>(character - '0');
        if (magnitude > (maximum - digit) / 10) {
            return std::nullopt;
        }
        magnitude = magnitude * 10 + digit;
        if (decimal_point) {
            if (denominator > std::numeric_limits<std::uint64_t>::max() / 10) {
                return std::nullopt;
            }
            denominator *= 10;
        }
        has_digit = true;
    }
    if (!has_digit) {
        return std::nullopt;
    }

    const auto numerator = static_cast<std::int64_t>(magnitude);
    return Rational{negative ? -numerator : numerator, denominator};
}

std::optional<Quantity> parse_quantity(const std::string_view value,
                                       const Quantity scale_factor) {
    const auto quantity = parse_decimal(value);
    if (!quantity.has_value() || quantity->numerator < 0 || scale_factor == 0) {
        return std::nullopt;
    }

    auto denominator = quantity->denominator;
    auto scale = scale_factor;
    const auto factor = std::gcd(denominator, scale);
    denominator /= factor;
    scale /= factor;
    if (denominator != 1) {
        return std::nullopt;
    }

    const auto magnitude = static_cast<std::uint64_t>(quantity->numerator);
    if (magnitude > std::numeric_limits<Quantity>::max() / scale) {
        return std::nullopt;
    }
    return magnitude * scale;
}

std::optional<TimestampNs> milliseconds_to_ns(const Json& value) {
    std::optional<std::uint64_t> milliseconds;
    if (value.is_string()) {
        milliseconds = parse_integer<std::uint64_t>(value.get_ref<const std::string&>());
    } else if (value.is_number_unsigned()) {
        milliseconds = value.get<std::uint64_t>();
    }
    if (!milliseconds.has_value() ||
        *milliseconds > std::numeric_limits<TimestampNs>::max() / 1'000'000) {
        return std::nullopt;
    }
    return *milliseconds * 1'000'000;
}

std::optional<TimestampNs> iso_timestamp_to_ns(const std::string_view value) {
    if (value.size() < 20 || value[4] != '-' || value[7] != '-' || value[10] != 'T' ||
        value[13] != ':' || value[16] != ':' || value.back() != 'Z') {
        return std::nullopt;
    }

    const auto year_value = parse_integer<int>(value.substr(0, 4));
    const auto month_value = parse_integer<unsigned>(value.substr(5, 2));
    const auto day_value = parse_integer<unsigned>(value.substr(8, 2));
    const auto hour_value = parse_integer<unsigned>(value.substr(11, 2));
    const auto minute_value = parse_integer<unsigned>(value.substr(14, 2));
    const auto second_value = parse_integer<unsigned>(value.substr(17, 2));
    if (!year_value.has_value() || !month_value.has_value() || !day_value.has_value() ||
        !hour_value.has_value() || !minute_value.has_value() || !second_value.has_value() ||
        *hour_value > 23 || *minute_value > 59 || *second_value > 59) {
        return std::nullopt;
    }

    const std::chrono::year_month_day date{std::chrono::year{*year_value},
                                           std::chrono::month{*month_value},
                                           std::chrono::day{*day_value}};
    if (!date.ok()) {
        return std::nullopt;
    }

    std::uint64_t fractional_ns = 0;
    if (value.size() > 20) {
        if (value[19] != '.') {
            return std::nullopt;
        }
        const auto fraction = value.substr(20, value.size() - 21);
        if (fraction.empty() || fraction.size() > 9) {
            return std::nullopt;
        }
        const auto parsed_fraction = parse_integer<std::uint64_t>(fraction);
        if (!parsed_fraction.has_value()) {
            return std::nullopt;
        }
        fractional_ns = *parsed_fraction;
        for (std::size_t digits = fraction.size(); digits < 9; ++digits) {
            fractional_ns *= 10;
        }
    }

    const auto time = std::chrono::sys_days{date} + std::chrono::hours{*hour_value} +
                      std::chrono::minutes{*minute_value} +
                      std::chrono::seconds{*second_value} +
                      std::chrono::nanoseconds{fractional_ns};
    const auto count = time.time_since_epoch().count();
    if (count < 0) {
        return std::nullopt;
    }
    return static_cast<TimestampNs>(count);
}

std::optional<Envelope> parse_envelope(const std::string_view raw_record) {
    const auto record = Json::parse(raw_record);
    if (!record.is_object() || !record.at("raw_payload").is_string()) {
        return std::nullopt;
    }
    return Envelope{record.at("local_receipt_timestamp_ns").get<TimestampNs>(),
                    Json::parse(record.at("raw_payload").get_ref<const std::string&>())};
}

bool append_update(const VenueConfig& config, const Side side, const std::string_view price,
                   const std::string_view quantity, const TimestampNs exchange_timestamp_ns,
                   const TimestampNs receipt_timestamp_ns, std::vector<BookUpdate>& output) {
    const auto rational_price = parse_decimal(price);
    const auto price_ticks = rational_price.has_value()
                                 ? fairvaluelab::price_to_ticks(*rational_price, config.tick_size)
                                 : std::nullopt;
    const auto scaled_quantity = parse_quantity(quantity, config.quantity_scale_factor);
    if (!price_ticks.has_value() || !scaled_quantity.has_value()) {
        return false;
    }
    output.push_back(BookUpdate{config.venue_id, side, *price_ticks, *scaled_quantity,
                                exchange_timestamp_ns, receipt_timestamp_ns, 0});
    return true;
}

bool append_trade(const VenueConfig& config, const TradeSide side, const std::string_view price,
                  const std::string_view quantity, const TimestampNs exchange_timestamp_ns,
                  const TimestampNs receipt_timestamp_ns, const std::uint64_t sequence_number,
                  std::vector<Trade>& output) {
    const auto rational_price = parse_decimal(price);
    const auto price_ticks = rational_price.has_value()
                                 ? fairvaluelab::price_to_ticks(*rational_price, config.tick_size)
                                 : std::nullopt;
    const auto scaled_quantity = parse_quantity(quantity, config.quantity_scale_factor);
    if (!price_ticks.has_value() || !scaled_quantity.has_value()) {
        return false;
    }
    output.push_back(Trade{config.venue_id, side, *price_ticks, *scaled_quantity,
                           exchange_timestamp_ns, receipt_timestamp_ns, sequence_number});
    return true;
}

std::uint64_t normalized_sequence(SequenceStatus status, std::uint64_t& next_sequence);

void assign_sequences(const SequenceStatus status, std::uint64_t& next_sequence,
                      std::vector<BookUpdate>& events) {
    if (status == SequenceStatus::Accepted) {
        for (auto& event : events) {
            event.sequence_number = next_sequence++;
        }
        return;
    }
    events.resize(1);
    events.front().sequence_number = normalized_sequence(status, next_sequence);
}

std::uint64_t normalized_sequence(const SequenceStatus status, std::uint64_t& next_sequence) {
    switch (status) {
    case SequenceStatus::Accepted:
        return next_sequence++;
    case SequenceStatus::Duplicate:
        return next_sequence - 1;
    case SequenceStatus::Stale:
        return next_sequence > 1 ? next_sequence - 2 : 0;
    case SequenceStatus::Gap:
        return next_sequence + 1;
    }
    return 0;
}

}

fairvaluelab::BinanceAdapter::BinanceAdapter(VenueConfig config) : config_(std::move(config)) {}

AdapterStatus fairvaluelab::BinanceAdapter::normalize(const std::string_view raw_record,
                                                      std::vector<BookUpdate>& output) const {
    output.clear();
    try {
        const auto envelope = parse_envelope(raw_record);
        if (!envelope.has_value()) {
            return AdapterStatus::Malformed;
        }
        const Json* payload = &envelope->payload;
        if (payload->contains("data")) {
            payload = &payload->at("data");
        }
        if (payload->contains("lastUpdateId")) {
            const auto& bids = payload->at("bids");
            const auto& asks = payload->at("asks");
            if (!bids.is_array() || !asks.is_array()) {
                return AdapterStatus::Malformed;
            }
            for (const auto& level : bids) {
                if (!level.is_array() || level.size() < 2 ||
                    !append_update(config_, Side::Bid,
                                   level.at(0).get_ref<const std::string&>(),
                                   level.at(1).get_ref<const std::string&>(),
                                   envelope->local_receipt_timestamp_ns,
                                   envelope->local_receipt_timestamp_ns, output)) {
                    output.clear();
                    return AdapterStatus::Malformed;
                }
            }
            for (const auto& level : asks) {
                if (!level.is_array() || level.size() < 2 ||
                    !append_update(config_, Side::Ask,
                                   level.at(0).get_ref<const std::string&>(),
                                   level.at(1).get_ref<const std::string&>(),
                                   envelope->local_receipt_timestamp_ns,
                                   envelope->local_receipt_timestamp_ns, output)) {
                    output.clear();
                    return AdapterStatus::Malformed;
                }
            }
            source_sequence_ = payload->at("lastUpdateId").get<std::uint64_t>();
            assign_sequences(SequenceStatus::Accepted, normalized_sequence_, output);
            return AdapterStatus::Accepted;
        }
        if (payload->value("e", "") != "depthUpdate") {
            return AdapterStatus::Unsupported;
        }

        const auto exchange_timestamp_ns = milliseconds_to_ns(payload->at("E"));
        const auto& bids = payload->at("b");
        const auto& asks = payload->at("a");
        if (!exchange_timestamp_ns.has_value() || !bids.is_array() || !asks.is_array()) {
            return AdapterStatus::Malformed;
        }
        for (const auto& level : bids) {
            if (!level.is_array() || level.size() < 2 ||
                !append_update(config_, Side::Bid, level.at(0).get_ref<const std::string&>(),
                               level.at(1).get_ref<const std::string&>(),
                               *exchange_timestamp_ns, envelope->local_receipt_timestamp_ns,
                               output)) {
                output.clear();
                return AdapterStatus::Malformed;
            }
        }
        for (const auto& level : asks) {
            if (!level.is_array() || level.size() < 2 ||
                !append_update(config_, Side::Ask, level.at(0).get_ref<const std::string&>(),
                               level.at(1).get_ref<const std::string&>(),
                               *exchange_timestamp_ns, envelope->local_receipt_timestamp_ns,
                               output)) {
                output.clear();
                return AdapterStatus::Malformed;
            }
        }
        if (output.empty()) {
            return AdapterStatus::Unsupported;
        }

        const auto final_sequence = payload->at("u").get<std::uint64_t>();
        const auto first_sequence = payload->value("U", final_sequence);
        SequenceStatus sequence_status = SequenceStatus::Accepted;
        if (source_sequence_.has_value()) {
            if (final_sequence == *source_sequence_) {
                sequence_status = SequenceStatus::Duplicate;
            } else if (final_sequence < *source_sequence_) {
                sequence_status = SequenceStatus::Stale;
            } else if (*source_sequence_ == std::numeric_limits<std::uint64_t>::max() ||
                       first_sequence > *source_sequence_ + 1) {
                sequence_status = SequenceStatus::Gap;
            }
        }
        if (sequence_status == SequenceStatus::Accepted ||
            sequence_status == SequenceStatus::Gap) {
            source_sequence_ = final_sequence;
        }
        assign_sequences(sequence_status, normalized_sequence_, output);
        return AdapterStatus::Accepted;
    } catch (const std::exception&) {
        output.clear();
        return AdapterStatus::Malformed;
    }
}

AdapterStatus fairvaluelab::BinanceAdapter::normalize_trades(
    const std::string_view raw_record, std::vector<Trade>& output) const {
    output.clear();
    try {
        const auto envelope = parse_envelope(raw_record);
        if (!envelope.has_value()) {
            return AdapterStatus::Malformed;
        }
        const Json* payload = &envelope->payload;
        if (payload->contains("data")) {
            payload = &payload->at("data");
        }
        if (payload->value("e", "") != "trade") {
            return AdapterStatus::Unsupported;
        }
        const auto exchange_timestamp_ns = milliseconds_to_ns(payload->at("T"));
        if (!exchange_timestamp_ns.has_value() ||
            !append_trade(config_, payload->at("m").get<bool>() ? TradeSide::Sell : TradeSide::Buy,
                          payload->at("p").get_ref<const std::string&>(),
                          payload->at("q").get_ref<const std::string&>(),
                          *exchange_timestamp_ns, envelope->local_receipt_timestamp_ns,
                          payload->at("t").get<std::uint64_t>(), output)) {
            output.clear();
            return AdapterStatus::Malformed;
        }
        return AdapterStatus::Accepted;
    } catch (const std::exception&) {
        output.clear();
        return AdapterStatus::Malformed;
    }
}

fairvaluelab::CoinbaseAdapter::CoinbaseAdapter(VenueConfig config) : config_(std::move(config)) {}

AdapterStatus fairvaluelab::CoinbaseAdapter::normalize(const std::string_view raw_record,
                                                       std::vector<BookUpdate>& output) const {
    output.clear();
    try {
        const auto envelope = parse_envelope(raw_record);
        if (!envelope.has_value()) {
            return AdapterStatus::Malformed;
        }
        const auto& payload = envelope->payload;
        const auto has_sequence = payload.contains("sequence_num") &&
                                  payload.at("sequence_num").is_number_unsigned();
        const auto source_sequence =
            has_sequence ? std::optional{payload.at("sequence_num").get<std::uint64_t>()}
                         : std::nullopt;
        if (payload.value("channel", "") != "l2_data") {
            if (source_sequence.has_value()) {
                if (source_sequence_.has_value() &&
                    (*source_sequence_ == std::numeric_limits<std::uint64_t>::max() ||
                     *source_sequence > *source_sequence_ + 1)) {
                    gap_pending_ = true;
                }
                if (!source_sequence_.has_value() || *source_sequence > *source_sequence_) {
                    source_sequence_ = *source_sequence;
                }
            }
            return AdapterStatus::Unsupported;
        }
        const auto& events = payload.at("events");
        if (!events.is_array()) {
            return AdapterStatus::Malformed;
        }

        for (const auto& event : events) {
            const auto& updates = event.at("updates");
            if (!updates.is_array()) {
                return AdapterStatus::Malformed;
            }
            for (const auto& update : updates) {
                const auto side_value = update.at("side").get_ref<const std::string&>();
                const auto side = side_value == "bid"   ? std::optional{Side::Bid}
                                  : side_value == "offer" ? std::optional{Side::Ask}
                                                          : std::nullopt;
                const auto exchange_timestamp_ns = iso_timestamp_to_ns(
                    update.at("event_time").get_ref<const std::string&>());
                if (!side.has_value() || !exchange_timestamp_ns.has_value() ||
                    !append_update(config_, *side,
                                   update.at("price_level").get_ref<const std::string&>(),
                                   update.at("new_quantity").get_ref<const std::string&>(),
                                   *exchange_timestamp_ns, envelope->local_receipt_timestamp_ns,
                                   output)) {
                    output.clear();
                    return AdapterStatus::Malformed;
                }
            }
        }
        if (output.empty() || !source_sequence.has_value()) {
            return AdapterStatus::Malformed;
        }
        SequenceStatus sequence_status = SequenceStatus::Accepted;
        if (source_sequence_.has_value()) {
            if (*source_sequence == *source_sequence_) {
                sequence_status = SequenceStatus::Duplicate;
            } else if (*source_sequence < *source_sequence_) {
                sequence_status = SequenceStatus::Stale;
            } else if (gap_pending_ ||
                       *source_sequence_ == std::numeric_limits<std::uint64_t>::max() ||
                       *source_sequence > *source_sequence_ + 1) {
                sequence_status = SequenceStatus::Gap;
            }
        }
        if (sequence_status == SequenceStatus::Accepted ||
            sequence_status == SequenceStatus::Gap) {
            source_sequence_ = *source_sequence;
            gap_pending_ = false;
        }
        assign_sequences(sequence_status, normalized_sequence_, output);
        return AdapterStatus::Accepted;
    } catch (const std::exception&) {
        output.clear();
        return AdapterStatus::Malformed;
    }
}

AdapterStatus fairvaluelab::CoinbaseAdapter::normalize_trades(
    const std::string_view raw_record, std::vector<Trade>& output) const {
    output.clear();
    try {
        const auto envelope = parse_envelope(raw_record);
        if (!envelope.has_value()) {
            return AdapterStatus::Malformed;
        }
        const auto& payload = envelope->payload;
        if (payload.value("channel", "") != "market_trades") {
            return AdapterStatus::Unsupported;
        }
        const auto& events = payload.at("events");
        if (!events.is_array()) {
            return AdapterStatus::Malformed;
        }
        for (const auto& event : events) {
            const auto& trades = event.at("trades");
            if (!trades.is_array()) {
                output.clear();
                return AdapterStatus::Malformed;
            }
            for (const auto& trade : trades) {
                const auto side_value = trade.at("side").get_ref<const std::string&>();
                const auto side = side_value == "BUY"    ? std::optional{TradeSide::Buy}
                                  : side_value == "SELL" ? std::optional{TradeSide::Sell}
                                                          : std::nullopt;
                const auto exchange_timestamp_ns =
                    iso_timestamp_to_ns(trade.at("time").get_ref<const std::string&>());
                const auto sequence_number = parse_integer<std::uint64_t>(
                    trade.at("trade_id").get_ref<const std::string&>());
                if (!side.has_value() || !exchange_timestamp_ns.has_value() ||
                    !sequence_number.has_value() ||
                    !append_trade(config_, *side,
                                  trade.at("price").get_ref<const std::string&>(),
                                  trade.at("size").get_ref<const std::string&>(),
                                  *exchange_timestamp_ns, envelope->local_receipt_timestamp_ns,
                                  *sequence_number, output)) {
                    output.clear();
                    return AdapterStatus::Malformed;
                }
            }
        }
        return output.empty() ? AdapterStatus::Unsupported : AdapterStatus::Accepted;
    } catch (const std::exception&) {
        output.clear();
        return AdapterStatus::Malformed;
    }
}

fairvaluelab::OkxAdapter::OkxAdapter(VenueConfig config) : config_(std::move(config)) {}

AdapterStatus fairvaluelab::OkxAdapter::normalize(const std::string_view raw_record,
                                                  std::vector<BookUpdate>& output) const {
    output.clear();
    try {
        const auto envelope = parse_envelope(raw_record);
        if (!envelope.has_value()) {
            return AdapterStatus::Malformed;
        }
        const auto& payload = envelope->payload;
        if (!payload.contains("arg") || payload.at("arg").value("channel", "") != "books") {
            return AdapterStatus::Unsupported;
        }
        if (!payload.contains("data")) {
            return AdapterStatus::Unsupported;
        }
        const auto& entries = payload.at("data");
        if (!entries.is_array() || entries.empty()) {
            return AdapterStatus::Malformed;
        }
        const auto& first_entry = entries.front();
        const auto source_sequence = first_entry.at("seqId").get<std::uint64_t>();
        for (const auto& entry : entries) {
            const auto exchange_timestamp_ns = milliseconds_to_ns(entry.at("ts"));
            const auto& bids = entry.at("bids");
            const auto& asks = entry.at("asks");
            if (!exchange_timestamp_ns.has_value() || !bids.is_array() || !asks.is_array() ||
                entry.at("seqId").get<std::uint64_t>() != source_sequence) {
                output.clear();
                return AdapterStatus::Malformed;
            }
            for (const auto& level : bids) {
                if (!level.is_array() || level.size() < 2 ||
                    !append_update(config_, Side::Bid,
                                   level.at(0).get_ref<const std::string&>(),
                                   level.at(1).get_ref<const std::string&>(),
                                   *exchange_timestamp_ns, envelope->local_receipt_timestamp_ns,
                                   output)) {
                    output.clear();
                    return AdapterStatus::Malformed;
                }
            }
            for (const auto& level : asks) {
                if (!level.is_array() || level.size() < 2 ||
                    !append_update(config_, Side::Ask,
                                   level.at(0).get_ref<const std::string&>(),
                                   level.at(1).get_ref<const std::string&>(),
                                   *exchange_timestamp_ns, envelope->local_receipt_timestamp_ns,
                                   output)) {
                    output.clear();
                    return AdapterStatus::Malformed;
                }
            }
        }
        if (output.empty()) {
            return AdapterStatus::Unsupported;
        }
        SequenceStatus sequence_status = SequenceStatus::Accepted;
        if (source_sequence_.has_value()) {
            if (source_sequence == *source_sequence_) {
                sequence_status = SequenceStatus::Duplicate;
            } else if (source_sequence < *source_sequence_) {
                sequence_status = SequenceStatus::Stale;
            } else if (first_entry.contains("prevSeqId") &&
                       first_entry.at("prevSeqId").get<std::int64_t>() !=
                           static_cast<std::int64_t>(*source_sequence_)) {
                sequence_status = SequenceStatus::Gap;
            }
        }
        if (sequence_status == SequenceStatus::Accepted ||
            sequence_status == SequenceStatus::Gap) {
            source_sequence_ = source_sequence;
        }
        assign_sequences(sequence_status, normalized_sequence_, output);
        return AdapterStatus::Accepted;
    } catch (const std::exception&) {
        output.clear();
        return AdapterStatus::Malformed;
    }
}

AdapterStatus fairvaluelab::OkxAdapter::normalize_trades(const std::string_view raw_record,
                                                        std::vector<Trade>& output) const {
    output.clear();
    try {
        const auto envelope = parse_envelope(raw_record);
        if (!envelope.has_value()) {
            return AdapterStatus::Malformed;
        }
        const auto& payload = envelope->payload;
        if (!payload.contains("arg") || payload.at("arg").value("channel", "") != "trades") {
            return AdapterStatus::Unsupported;
        }
        if (!payload.contains("data")) {
            return AdapterStatus::Unsupported;
        }
        const auto& trades = payload.at("data");
        if (!trades.is_array()) {
            return AdapterStatus::Malformed;
        }
        for (const auto& trade : trades) {
            const auto side_value = trade.at("side").get_ref<const std::string&>();
            const auto side = side_value == "buy"    ? std::optional{TradeSide::Buy}
                              : side_value == "sell" ? std::optional{TradeSide::Sell}
                                                     : std::nullopt;
            const auto exchange_timestamp_ns = milliseconds_to_ns(trade.at("ts"));
            const auto sequence_number = parse_integer<std::uint64_t>(
                trade.at("tradeId").get_ref<const std::string&>());
            if (!side.has_value() || !exchange_timestamp_ns.has_value() ||
                !sequence_number.has_value() ||
                !append_trade(config_, *side, trade.at("px").get_ref<const std::string&>(),
                              trade.at("sz").get_ref<const std::string&>(),
                              *exchange_timestamp_ns, envelope->local_receipt_timestamp_ns,
                              *sequence_number, output)) {
                output.clear();
                return AdapterStatus::Malformed;
            }
        }
        return output.empty() ? AdapterStatus::Unsupported : AdapterStatus::Accepted;
    } catch (const std::exception&) {
        output.clear();
        return AdapterStatus::Malformed;
    }
}
