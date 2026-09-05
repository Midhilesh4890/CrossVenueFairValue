#include "fairvaluelab/capture_validation.hpp"

#include <cstddef>
#include <iostream>
#include <string>

#define FVL_CHECK(condition)                                                                       \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            std::cerr << __FILE__ << ':' << __LINE__ << ": check failed: " #condition << '\n';     \
            return false;                                                                          \
        }                                                                                          \
    } while (false)

using fairvaluelab::CaptureContinuityStatus;
using fairvaluelab::CaptureSequenceState;

std::string escaped(const std::string& value) {
    std::string output;
    for (const char character : value) {
        if (character == '"' || character == '\\') {
            output += '\\';
        }
        output += character;
    }
    return output;
}

std::string wrapped(const std::string& payload) {
    return "{\"local_receipt_timestamp_ns\":1,\"raw_payload\":\"" + escaped(payload) + "\"}";
}

std::string binance_depth(const int first, const int final) {
    return wrapped("{\"e\":\"depthUpdate\",\"E\":1,\"U\":" + std::to_string(first) +
                   ",\"u\":" + std::to_string(final) + ",\"b\":[],\"a\":[]}");
}

std::string binance_depth_with_previous(const int first, const int final, const int previous) {
    return wrapped("{\"e\":\"depthUpdate\",\"E\":1,\"U\":" + std::to_string(first) +
                   ",\"u\":" + std::to_string(final) + ",\"pu\":" +
                   std::to_string(previous) + ",\"b\":[],\"a\":[]}");
}

std::string coinbase_message(const int sequence) {
    return wrapped("{\"channel\":\"l2_data\",\"sequence_num\":" + std::to_string(sequence) +
                   ",\"events\":[]}");
}

std::string okx_message(const int sequence, const int previous) {
    return wrapped("{\"arg\":{\"channel\":\"books\"},\"data\":[{\"seqId\":" +
                   std::to_string(sequence) + ",\"prevSeqId\":" + std::to_string(previous) +
                   ",\"ts\":\"1\",\"bids\":[],\"asks\":[]}]}");
}

bool test_shape_derivation() {
    const auto shape = fairvaluelab::derive_payload_shape(
        wrapped("{\"b\":2,\"a\":{\"z\":true,\"x\":\"value\"},\"c\":[1,\"two\",null]}")
    );
    FVL_CHECK(shape == "{a:{x:string,z:boolean},b:integer,c:[integer,string,null]}");
    FVL_CHECK(fairvaluelab::derive_payload_shape("not json") == "invalid-json");
    return true;
}

bool test_binance_clean_chain() {
    CaptureSequenceState state;
    FVL_CHECK(fairvaluelab::check_binance_sequence(binance_depth(10, 12), state) ==
              CaptureContinuityStatus::Accepted);
    FVL_CHECK(fairvaluelab::check_binance_sequence(binance_depth(13, 15), state) ==
              CaptureContinuityStatus::Accepted);
    return true;
}

bool test_binance_missing_message() {
    CaptureSequenceState state;
    FVL_CHECK(fairvaluelab::check_binance_sequence(binance_depth(10, 12), state) ==
              CaptureContinuityStatus::Accepted);
    FVL_CHECK(fairvaluelab::check_binance_sequence(binance_depth(14, 16), state) ==
              CaptureContinuityStatus::Gap);
    return true;
}

bool test_binance_previous_update_mismatch() {
    CaptureSequenceState state;
    FVL_CHECK(fairvaluelab::check_binance_sequence(binance_depth(10, 12), state) ==
              CaptureContinuityStatus::Accepted);
    FVL_CHECK(fairvaluelab::check_binance_sequence(binance_depth_with_previous(13, 15, 11), state) ==
              CaptureContinuityStatus::Gap);
    return true;
}

bool test_binance_duplicate() {
    CaptureSequenceState state;
    FVL_CHECK(fairvaluelab::check_binance_sequence(binance_depth(10, 12), state) ==
              CaptureContinuityStatus::Accepted);
    FVL_CHECK(fairvaluelab::check_binance_sequence(binance_depth(10, 12), state) ==
              CaptureContinuityStatus::Duplicate);
    return true;
}

bool test_binance_out_of_order() {
    CaptureSequenceState state;
    FVL_CHECK(fairvaluelab::check_binance_sequence(binance_depth(10, 12), state) ==
              CaptureContinuityStatus::Accepted);
    FVL_CHECK(fairvaluelab::check_binance_sequence(binance_depth(8, 9), state) ==
              CaptureContinuityStatus::OutOfOrder);
    return true;
}

bool test_coinbase_clean_chain() {
    CaptureSequenceState state;
    FVL_CHECK(fairvaluelab::check_coinbase_sequence(coinbase_message(1), state) ==
              CaptureContinuityStatus::Accepted);
    FVL_CHECK(fairvaluelab::check_coinbase_sequence(coinbase_message(2), state) ==
              CaptureContinuityStatus::Accepted);
    return true;
}

bool test_coinbase_missing_message() {
    CaptureSequenceState state;
    FVL_CHECK(fairvaluelab::check_coinbase_sequence(coinbase_message(1), state) ==
              CaptureContinuityStatus::Accepted);
    FVL_CHECK(fairvaluelab::check_coinbase_sequence(coinbase_message(3), state) ==
              CaptureContinuityStatus::Gap);
    return true;
}

bool test_coinbase_duplicate() {
    CaptureSequenceState state;
    FVL_CHECK(fairvaluelab::check_coinbase_sequence(coinbase_message(1), state) ==
              CaptureContinuityStatus::Accepted);
    FVL_CHECK(fairvaluelab::check_coinbase_sequence(coinbase_message(1), state) ==
              CaptureContinuityStatus::Duplicate);
    return true;
}

bool test_coinbase_out_of_order() {
    CaptureSequenceState state;
    FVL_CHECK(fairvaluelab::check_coinbase_sequence(coinbase_message(2), state) ==
              CaptureContinuityStatus::Accepted);
    FVL_CHECK(fairvaluelab::check_coinbase_sequence(coinbase_message(1), state) ==
              CaptureContinuityStatus::OutOfOrder);
    return true;
}

bool test_okx_clean_chain() {
    CaptureSequenceState state;
    FVL_CHECK(fairvaluelab::check_okx_sequence(okx_message(1, 0), state) ==
              CaptureContinuityStatus::Accepted);
    FVL_CHECK(fairvaluelab::check_okx_sequence(okx_message(2, 1), state) ==
              CaptureContinuityStatus::Accepted);
    return true;
}

bool test_okx_missing_message() {
    CaptureSequenceState state;
    FVL_CHECK(fairvaluelab::check_okx_sequence(okx_message(1, 0), state) ==
              CaptureContinuityStatus::Accepted);
    FVL_CHECK(fairvaluelab::check_okx_sequence(okx_message(3, 2), state) ==
              CaptureContinuityStatus::Gap);
    return true;
}

bool test_okx_duplicate() {
    CaptureSequenceState state;
    FVL_CHECK(fairvaluelab::check_okx_sequence(okx_message(1, 0), state) ==
              CaptureContinuityStatus::Accepted);
    FVL_CHECK(fairvaluelab::check_okx_sequence(okx_message(1, 0), state) ==
              CaptureContinuityStatus::Duplicate);
    return true;
}

bool test_okx_out_of_order() {
    CaptureSequenceState state;
    FVL_CHECK(fairvaluelab::check_okx_sequence(okx_message(2, 1), state) ==
              CaptureContinuityStatus::Accepted);
    FVL_CHECK(fairvaluelab::check_okx_sequence(okx_message(1, 0), state) ==
              CaptureContinuityStatus::OutOfOrder);
    return true;
}

struct TestCase {
    const char* name;
    bool (*run)();
};

int main() {
    constexpr TestCase tests[]{
        {"shape derivation", test_shape_derivation},
        {"binance clean chain", test_binance_clean_chain},
        {"binance missing message", test_binance_missing_message},
        {"binance previous update mismatch", test_binance_previous_update_mismatch},
        {"binance duplicate", test_binance_duplicate},
        {"binance out of order", test_binance_out_of_order},
        {"coinbase clean chain", test_coinbase_clean_chain},
        {"coinbase missing message", test_coinbase_missing_message},
        {"coinbase duplicate", test_coinbase_duplicate},
        {"coinbase out of order", test_coinbase_out_of_order},
        {"okx clean chain", test_okx_clean_chain},
        {"okx missing message", test_okx_missing_message},
        {"okx duplicate", test_okx_duplicate},
        {"okx out of order", test_okx_out_of_order},
    };

    std::size_t passed = 0;
    for (const auto& test : tests) {
        if (!test.run()) {
            std::cerr << "FAILED: " << test.name << '\n';
            return 1;
        }
        ++passed;
        std::cout << "PASSED: " << test.name << '\n';
    }
    std::cout << passed << " tests passed\n";
    return 0;
}
