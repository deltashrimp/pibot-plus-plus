cmake_minimum_required(VERSION 3.16)
project(pibot-tools LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Threads REQUIRED)
find_package(spdlog REQUIRED)
find_package(oatpp REQUIRED)

file(GLOB PIBOT_TOOLS_SOURCES
    CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*/*.cpp"
)

add_executable(pibot-tools ${PIBOT_TOOLS_SOURCES})

target_include_directories(pibot-tools PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(pibot-tools PRIVATE
    Threads::Threads
    spdlog::spdlog
    oatpp::oatpp
)
