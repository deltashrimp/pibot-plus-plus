cmake_minimum_required(VERSION 3.16)
project(pibot-auto-mod LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(
    tomlplusplus
    GIT_REPOSITORY https://github.com/marzer/tomlplusplus.git
    GIT_TAG v3.4.0
)
FetchContent_MakeAvailable(tomlplusplus)

find_package(Threads REQUIRED)
find_package(spdlog REQUIRED)
find_package(oatpp REQUIRED)

file(GLOB PIBOT_AUTO_MOD_SOURCES
    CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)

add_executable(pibot-auto-mod ${PIBOT_AUTO_MOD_SOURCES})

target_include_directories(pibot-auto-mod PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/../common
)

target_link_libraries(pibot-auto-mod PRIVATE
    Threads::Threads
    spdlog::spdlog
    oatpp::oatpp
    tomlplusplus::tomlplusplus
)
