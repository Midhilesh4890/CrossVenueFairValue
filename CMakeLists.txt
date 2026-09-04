cmake_minimum_required(VERSION 3.20)

project(FairValueLab VERSION 0.1.0 LANGUAGES CXX)

include(CTest)

option(FVL_SANITIZE "Enable address and undefined behavior sanitizers" OFF)

if(FVL_SANITIZE AND NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    message(FATAL_ERROR "FVL_SANITIZE requires GCC or Clang")
endif()

function(fvl_enable_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive-)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

function(fvl_enable_sanitizers target)
    if(FVL_SANITIZE)
        target_compile_options(
            ${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer
        )
        target_link_options(
            ${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer
        )
    endif()
endfunction()

add_library(fairvaluelab_order_book cpp/src/order_book.cpp cpp/src/venue.cpp)
add_library(FairValueLab::order_book ALIAS fairvaluelab_order_book)
target_include_directories(
    fairvaluelab_order_book
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/cpp/include>
)
target_compile_features(fairvaluelab_order_book PUBLIC cxx_std_20)
set_target_properties(fairvaluelab_order_book PROPERTIES CXX_EXTENSIONS OFF)
fvl_enable_warnings(fairvaluelab_order_book)
fvl_enable_sanitizers(fairvaluelab_order_book)

add_executable(fvl_replay cpp/src/replay.cpp)
target_link_libraries(fvl_replay PRIVATE FairValueLab::order_book)
fvl_enable_warnings(fvl_replay)
fvl_enable_sanitizers(fvl_replay)

add_executable(fvl_order_book_benchmark cpp/benchmark/order_book_benchmark.cpp)
target_link_libraries(fvl_order_book_benchmark PRIVATE FairValueLab::order_book)
fvl_enable_warnings(fvl_order_book_benchmark)
fvl_enable_sanitizers(fvl_order_book_benchmark)

if(BUILD_TESTING)
    add_executable(fvl_order_book_test tests/cpp/order_book_test.cpp)
    target_link_libraries(fvl_order_book_test PRIVATE FairValueLab::order_book)
    fvl_enable_warnings(fvl_order_book_test)
    fvl_enable_sanitizers(fvl_order_book_test)
    add_test(NAME order_book COMMAND fvl_order_book_test)

    add_executable(fvl_replay_determinism_test tests/cpp/replay_determinism_test.cpp)
    target_link_libraries(fvl_replay_determinism_test PRIVATE FairValueLab::order_book)
    target_compile_definitions(
        fvl_replay_determinism_test
        PRIVATE FVL_FIXTURE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/data/fixtures/order_book_updates.csv"
    )
    fvl_enable_warnings(fvl_replay_determinism_test)
    fvl_enable_sanitizers(fvl_replay_determinism_test)
    add_test(NAME replay_determinism COMMAND fvl_replay_determinism_test)

    add_executable(fvl_venue_test tests/cpp/venue_test.cpp)
    target_link_libraries(fvl_venue_test PRIVATE FairValueLab::order_book)
    fvl_enable_warnings(fvl_venue_test)
    fvl_enable_sanitizers(fvl_venue_test)
    add_test(NAME venue COMMAND fvl_venue_test)
endif()
