cmake_minimum_required(VERSION 3.16)
project(pibot-rp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(spdlog REQUIRED)
find_package(oatpp REQUIRED)
# hiredis and redis++ are built from source in the Docker image; their CMake
# configs export the namespaced targets `hiredis::hiredis` and `redis++::redis++`.
find_package(hiredis REQUIRED)
find_package(redis++ REQUIRED)

file(GLOB PIBOT_RP_SOURCES
    CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*/*.cpp"
)

add_executable(pibot-rp ${PIBOT_RP_SOURCES})

target_include_directories(pibot-rp PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(pibot-rp PRIVATE
    Threads::Threads
    OpenSSL::SSL
    OpenSSL::Crypto
    spdlog::spdlog
    oatpp::oatpp
    hiredis::hiredis
    redis++::redis++
)
