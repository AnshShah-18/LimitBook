cmake_minimum_required(VERSION 3.20)
project(Aquila VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(AQUILA_BUILD_TESTS "Build unit and integration tests" ON)
option(AQUILA_BUILD_BENCHMARKS "Build Google Benchmark suites" ON)
option(AQUILA_NATIVE "Compile with -march=native" OFF)
option(AQUILA_SANITIZE "Build with AddressSanitizer + UBSan" OFF)

if(MSVC)
  add_compile_options(/W4)
else()
  add_compile_options(-Wall -Wextra -Wpedantic)
  if(AQUILA_NATIVE)
    add_compile_options(-march=native)
  endif()
  if(AQUILA_SANITIZE)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
  endif()
endif()

# ---------------------------------------------------------------- core library
add_library(aquila_core
  src/order.cpp
  src/order_book.cpp
  src/pooled_order_book.cpp
  src/matching_engine.cpp
  src/event.cpp
  src/event_generator.cpp
  src/event_queue.cpp
)
target_include_directories(aquila_core PUBLIC include)
find_package(Threads REQUIRED)
target_link_libraries(aquila_core PUBLIC Threads::Threads)

add_executable(aquila src/main.cpp)
target_link_libraries(aquila PRIVATE aquila_core)

add_executable(alloc_profile tools/alloc_profile.cpp)
target_link_libraries(alloc_profile PRIVATE aquila_core)

# ---------------------------------------------------------------- dependencies
# Fetched from GitHub at configure time. To build offline, point CMake at local
# checkouts: -DFETCHCONTENT_SOURCE_DIR_GOOGLETEST=/path -DFETCHCONTENT_SOURCE_DIR_BENCHMARK=/path
include(FetchContent)

if(AQUILA_BUILD_TESTS)
  FetchContent_Declare(googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.15.2
    GIT_SHALLOW TRUE)
  set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
  set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(googletest)

  enable_testing()
  add_executable(aquila_tests
    tests/order_book_test.cpp
    tests/matching_engine_test.cpp
    tests/event_test.cpp
    tests/queue_test.cpp
    tests/integration_test.cpp)
  target_link_libraries(aquila_tests PRIVATE aquila_core GTest::gtest_main)
  include(GoogleTest)
  gtest_discover_tests(aquila_tests DISCOVERY_TIMEOUT 60)
endif()

if(AQUILA_BUILD_BENCHMARKS)
  FetchContent_Declare(benchmark
    GIT_REPOSITORY https://github.com/google/benchmark.git
    GIT_TAG v1.9.1
    GIT_SHALLOW TRUE)
  set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
  set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
  set(BENCHMARK_ENABLE_WERROR OFF CACHE BOOL "" FORCE)  # compiler-version warnings must not break the build
  set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(benchmark)

  foreach(bench order_book_benchmark matching_engine_benchmark queue_benchmark)
    add_executable(${bench} benchmarks/${bench}.cpp)
    target_link_libraries(${bench} PRIVATE aquila_core benchmark::benchmark)
  endforeach()
endif()
