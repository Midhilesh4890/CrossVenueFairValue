#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace fairvaluelab {

enum class CaptureContinuityStatus : std::uint8_t {
    Accepted,
    Duplicate,
    Gap,
    OutOfOrder,
    Unsupported,
    Malformed,
};

enum class CaptureChecksumStatus : std::uint8_t {
    Unsupported,
    Valid,
    Mismatch,
    Malformed,
};

struct CaptureSequenceState {
    std::optional<std::uint64_t> last_sequence;
};

[[nodiscard]] std::string derive_payload_shape(std::string_view raw_record);
[[nodiscard]] CaptureContinuityStatus check_binance_sequence(std::string_view raw_record,
                                                             CaptureSequenceState& state);
[[nodiscard]] CaptureContinuityStatus check_coinbase_sequence(std::string_view raw_record,
                                                              CaptureSequenceState& state);
[[nodiscard]] CaptureContinuityStatus check_okx_sequence(std::string_view raw_record,
                                                         CaptureSequenceState& state);
[[nodiscard]] CaptureChecksumStatus check_okx_checksum(std::string_view raw_record);

}
