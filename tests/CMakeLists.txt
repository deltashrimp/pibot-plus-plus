cmake_minimum_required(VERSION 3.16)
project(pibot-tests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Threads REQUIRED)
find_package(spdlog REQUIRED)
find_package(GTest REQUIRED)
include(GoogleTest)

set(CORE_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../core/src)
set(AUTO_MOD_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../auto-mod/src)

# core/src/utils/helpers — pure logic, no service dependencies.
add_executable(test_helpers
    test_helpers.cpp
    ${CORE_SRC_DIR}/utils/helpers.cpp
)
target_include_directories(test_helpers PRIVATE ${CORE_SRC_DIR})
target_link_libraries(test_helpers PRIVATE
    Threads::Threads
    spdlog::spdlog
    GTest::gtest_main
)

# auto-mod AutoModerator — keyword filter + sliding-window anti-spam.
# The test TU #includes automoderator.cpp to reach its file-local helpers,
# so the source is NOT compiled separately here.
add_executable(test_automoderator
    test_automoderator.cpp
)
target_include_directories(test_automoderator PRIVATE ${AUTO_MOD_SRC_DIR})
target_link_libraries(test_automoderator PRIVATE
    Threads::Threads
    spdlog::spdlog
    GTest::gtest_main
)

# auto-mod Logger — structured JSON formatter, same include-the-source trick.
add_executable(test_logger
    test_logger.cpp
)
target_include_directories(test_logger PRIVATE ${AUTO_MOD_SRC_DIR})
target_link_libraries(test_logger PRIVATE
    Threads::Threads
    spdlog::spdlog
    GTest::gtest_main
)

enable_testing()
gtest_discover_tests(test_helpers)
gtest_discover_tests(test_automoderator)
gtest_discover_tests(test_logger)
