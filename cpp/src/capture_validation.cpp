#include "fairvaluelab/capture_validation.hpp"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <string>
#include <string_view>

namespace {

using Json = nlohmann::json;
using fairvaluelab::CaptureContinuityStatus;
using fairvaluelab::CaptureSequenceState;

std::string scalar_shape(const Json& value) {
    if (value.is_null()) {
        return "null";
    }
    if (value.is_boolean()) {
        return "boolean";
    }
    if (value.is_number_integer() || value.is_number_unsigned()) {
        return "integer";
    }
    if (value.is_number_float()) {
        return "number";
    }
    if (value.is_string()) {
        return "string";
    }
    if (value.is_binary()) {
        return "binary";
    }
    return "discarded";
}

std::string shape_of(const Json& value) {
    if (value.is_array()) {
        std::string output = "[";
        bool first = true;
        for (const auto& item : value) {
            if (!first) {
                output += ',';
            }
            first = false;
            output += shape_of(item);
        }
        output += ']';
        return output;
    }
    if (value.is_object()) {
        std::string output = "{";
        bool first = true;
        for (auto iterator = value.begin(); iterator != value.end(); ++iterator) {
            if (!first) {
                output += ',';
            }
            first = false;
            output += iterator.key();
            output += ':';
            output += shape_of(iterator.value());
        }
        output += '}';
        return output;
    }
    return scalar_shape(value);
}

Json payload_from_record(const std::string_view raw_record) {
    const auto record = Json::parse(raw_record);
    if (!record.is_object() || !record.contains("raw_payload") ||
        !record.at("raw_payload").is_string()) {
        return record;
    }
    return Json::parse(record.at("raw_payload").get_ref<const std::string&>());
}

const Json& binance_payload(const Json& payload) {
    if (payload.is_object() && payload.contains("data") && payload.at("data").is_object()) {
        return payload.at("data");
    }
    return payload;
}

CaptureContinuityStatus compare_sequence(const std::uint64_t sequence,
                                          CaptureSequenceState& state) {
    if (!state.last_sequence.has_value()) {
        state.last_sequence = sequence;
        return CaptureContinuityStatus::Accepted;
    }
    const auto last = *state.last_sequence;
    if (sequence == last) {
        return CaptureContinuityStatus::Duplicate;
    }
    if (sequence < last) {
        return CaptureContinuityStatus::OutOfOrder;
    }
    if (last == UINT64_MAX || sequence > last + 1) {
        state.last_sequence = sequence;
        return CaptureContinuityStatus::Gap;
    }
    state.last_sequence = sequence;
    return CaptureContinuityStatus::Accepted;
}

}

std::string fairvaluelab::derive_payload_shape(const std::string_view raw_record) {
    try {
        return shape_of(payload_from_record(raw_record));
    } catch (const std::exception&) {
        return "invalid-json";
    }
}

CaptureContinuityStatus fairvaluelab::check_binance_sequence(
    const std::string_view raw_record, CaptureSequenceState& state) {
    try {
        const auto payload_storage = payload_from_record(raw_record);
        const auto& payload = binance_payload(payload_storage);
        if (!payload.is_object()) {
            return CaptureContinuityStatus::Malformed;
        }
        if (payload.contains("lastUpdateId")) {
            const auto sequence = payload.at("lastUpdateId").get<std::uint64_t>();
            state.last_sequence = sequence;
            return CaptureContinuityStatus::Accepted;
        }
        if (payload.value("e", "") != "depthUpdate") {
            return CaptureContinuityStatus::Unsupported;
        }
        const auto first_sequence = payload.at("U").get<std::uint64_t>();
        const auto final_sequence = payload.at("u").get<std::uint64_t>();
        if (final_sequence < first_sequence) {
            return CaptureContinuityStatus::Malformed;
        }
        if (!state.last_sequence.has_value()) {
            state.last_sequence = final_sequence;
            return CaptureContinuityStatus::Accepted;
        }
        const auto last = *state.last_sequence;
        if (final_sequence == last) {
            return CaptureContinuityStatus::Duplicate;
        }
        if (final_sequence < last) {
            return CaptureContinuityStatus::OutOfOrder;
        }
        bool gap = last == UINT64_MAX || first_sequence > last + 1;
        if (payload.contains("pu")) {
            gap = gap || payload.at("pu").get<std::uint64_t>() != last;
        }
        state.last_sequence = final_sequence;
        return gap ? CaptureContinuityStatus::Gap : CaptureContinuityStatus::Accepted;
    } catch (const std::exception&) {
        return CaptureContinuityStatus::Malformed;
    }
}

CaptureContinuityStatus fairvaluelab::check_coinbase_sequence(
    const std::string_view raw_record, CaptureSequenceState& state) {
    try {
        const auto payload = payload_from_record(raw_record);
        if (!payload.is_object()) {
            return CaptureContinuityStatus::Malformed;
        }
        if (!payload.contains("sequence_num")) {
            return CaptureContinuityStatus::Unsupported;
        }
        return compare_sequence(payload.at("sequence_num").get<std::uint64_t>(), state);
    } catch (const std::exception&) {
        return CaptureContinuityStatus::Malformed;
    }
}

CaptureContinuityStatus fairvaluelab::check_okx_sequence(const std::string_view raw_record,
                                                         CaptureSequenceState& state) {
    try {
        const auto payload = payload_from_record(raw_record);
        if (!payload.is_object() || !payload.contains("arg") || !payload.at("arg").is_object() ||
            payload.at("arg").value("channel", "") != "books") {
            return CaptureContinuityStatus::Unsupported;
        }
        if (!payload.contains("data") || !payload.at("data").is_array() ||
            payload.at("data").empty()) {
            return CaptureContinuityStatus::Malformed;
        }
        const auto& entry = payload.at("data").front();
        const auto sequence = entry.at("seqId").get<std::uint64_t>();
        if (!state.last_sequence.has_value()) {
            state.last_sequence = sequence;
            return CaptureContinuityStatus::Accepted;
        }
        const auto last = *state.last_sequence;
        if (sequence == last) {
            return CaptureContinuityStatus::Duplicate;
        }
        if (sequence < last) {
            return CaptureContinuityStatus::OutOfOrder;
        }
        bool gap = false;
        if (entry.contains("prevSeqId") && !entry.at("prevSeqId").is_null()) {
            const auto previous = entry.at("prevSeqId").get<std::int64_t>();
            gap = previous != static_cast<std::int64_t>(last);
        } else {
            gap = last == UINT64_MAX || sequence > last + 1;
        }
        state.last_sequence = sequence;
        return gap ? CaptureContinuityStatus::Gap : CaptureContinuityStatus::Accepted;
    } catch (const std::exception&) {
        return CaptureContinuityStatus::Malformed;
    }
}

fairvaluelab::CaptureChecksumStatus fairvaluelab::check_okx_checksum(
    const std::string_view raw_record) {
    try {
        const auto payload = payload_from_record(raw_record);
        if (!payload.is_object() || !payload.contains("arg") || !payload.at("arg").is_object() ||
            payload.at("arg").value("channel", "") != "books") {
            return CaptureChecksumStatus::Unsupported;
        }
        return CaptureChecksumStatus::Unsupported;
    } catch (const std::exception&) {
        return CaptureChecksumStatus::Malformed;
    }
}
