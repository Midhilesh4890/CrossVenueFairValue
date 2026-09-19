cmake_minimum_required(VERSION 3.20)

project(FairValueLab VERSION 0.1.0 LANGUAGES CXX)

include(CTest)
include(FetchContent)

FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(nlohmann_json)

option(FVL_SANITIZE "Enable address and undefined behavior sanitizers" OFF)

if(FVL_SANITIZE AND NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    message(FATAL_ERROR "FVL_SANITIZE requires GCC or Clang")
endif()

function(fvl_configure_target target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive-)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    if(FVL_SANITIZE)
        target_compile_options(
            ${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer
        )
        target_link_options(
            ${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer
        )
    endif()
endfunction()

add_library(
    fairvaluelab_order_book
    cpp/src/capture_converter.cpp
    cpp/src/capture_validation.cpp
    cpp/src/cross_venue.cpp
    cpp/src/feature_emitter.cpp
    cpp/src/multi_replay.cpp
    cpp/src/normalized_csv.cpp
    cpp/src/order_book.cpp
    cpp/src/research_sampler.cpp
    cpp/src/research_dataset.cpp
    cpp/src/venue.cpp
    cpp/src/venue_adapters.cpp
)
add_library(FairValueLab::order_book ALIAS fairvaluelab_order_book)
target_include_directories(
    fairvaluelab_order_book
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/cpp/include>
)
target_compile_features(fairvaluelab_order_book PUBLIC cxx_std_20)
target_link_libraries(fairvaluelab_order_book PRIVATE nlohmann_json::nlohmann_json)
set_target_properties(fairvaluelab_order_book PROPERTIES CXX_EXTENSIONS OFF)
fvl_configure_target(fairvaluelab_order_book)

function(fvl_add_executable target source)
    add_executable(${target} ${source})
    target_link_libraries(${target} PRIVATE FairValueLab::order_book)
    fvl_configure_target(${target})
endfunction()

fvl_add_executable(fvl_replay cpp/src/replay.cpp)

fvl_add_executable(fvl_convert_capture cpp/src/convert_capture.cpp)

fvl_add_executable(fvl_validate_capture cpp/src/validate_capture.cpp)

fvl_add_executable(fvl_multi_replay cpp/src/multi_replay_main.cpp)

fvl_add_executable(fvl_features cpp/src/features_main.cpp)

fvl_add_executable(fvl_dataset cpp/src/dataset_main.cpp)

fvl_add_executable(fvl_order_book_benchmark cpp/benchmark/order_book_benchmark.cpp)

fvl_add_executable(fvl_cross_venue_benchmark cpp/benchmark/cross_venue_benchmark.cpp)

if(BUILD_TESTING)
    fvl_add_executable(fvl_order_book_test tests/cpp/order_book_test.cpp)
    add_test(NAME order_book COMMAND fvl_order_book_test)

    fvl_add_executable(fvl_replay_determinism_test tests/cpp/replay_determinism_test.cpp)
    target_compile_definitions(
        fvl_replay_determinism_test
        PRIVATE FVL_FIXTURE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/data/fixtures/order_book_updates.csv"
    )
    add_test(NAME replay_determinism COMMAND fvl_replay_determinism_test)

    fvl_add_executable(fvl_venue_test tests/cpp/venue_test.cpp)
    target_compile_definitions(
        fvl_venue_test
        PRIVATE FVL_KRAKEN_NUMERIC_FIXTURE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/data/fixtures/kraken_numeric_quantity.ndjson"
    )
    add_test(NAME venue COMMAND fvl_venue_test)

    fvl_add_executable(fvl_multi_venue_test tests/cpp/multi_venue_test.cpp)
    target_compile_definitions(
        fvl_multi_venue_test
        PRIVATE FVL_MULTI_FIXTURE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/data/fixtures/multi_venue"
    )
    add_test(NAME multi_venue COMMAND fvl_multi_venue_test)

    fvl_add_executable(fvl_feature_test tests/cpp/feature_test.cpp)
    add_test(NAME features COMMAND fvl_feature_test)

    fvl_add_executable(fvl_capture_validation_test tests/cpp/capture_validation_test.cpp)
    add_test(NAME capture_validation COMMAND fvl_capture_validation_test)

    fvl_add_executable(fvl_feature_emitter_allocation_test tests/cpp/feature_emitter_allocation_test.cpp)
    add_test(NAME feature_emitter_accepted_allocation
             COMMAND fvl_feature_emitter_allocation_test accepted)
    add_test(NAME feature_emitter_rejected_allocation
             COMMAND fvl_feature_emitter_allocation_test rejected)

    fvl_add_executable(fvl_cross_venue_test tests/cpp/cross_venue_test.cpp)
    add_test(NAME cross_venue COMMAND fvl_cross_venue_test)

    fvl_add_executable(fvl_cross_venue_allocation_test tests/cpp/cross_venue_allocation_test.cpp)
    add_test(NAME cross_venue_allocation COMMAND fvl_cross_venue_allocation_test)

    fvl_add_executable(fvl_research_sampler_test tests/cpp/research_sampler_test.cpp)
    add_test(NAME research_sampler COMMAND fvl_research_sampler_test)

    fvl_add_executable(fvl_research_dataset_test tests/cpp/research_dataset_test.cpp)
    target_compile_definitions(
        fvl_research_dataset_test
        PRIVATE FVL_RESEARCH_FIXTURE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/data/fixtures/multi_venue_updates.csv"
    )
    add_test(NAME research_dataset COMMAND fvl_research_dataset_test)
endif()
