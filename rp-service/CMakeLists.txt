cmake_minimum_required(VERSION 3.16)
project(pibot-rp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(PostgreSQL REQUIRED)
find_package(spdlog REQUIRED)
find_package(oatpp REQUIRED)
# Upstream renamed the module to oatpp-postgresql (>= 1.3.0); older
# distributions (e.g. Arch AUR) still install it as oatpp-pqsql.
find_package(oatpp-postgresql QUIET)
if(NOT oatpp-postgresql_FOUND)
    find_package(oatpp-pqsql REQUIRED)
    set(OATPP_PQSQL_LIBRARIES oatpp-pqsql)
else()
    set(OATPP_PQSQL_LIBRARIES oatpp::oatpp-postgresql)
endif()
find_package(hiredis REQUIRED)
find_package(redis++ REQUIRED)

file(GLOB PIBOT_RP_SOURCES
    CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)

add_executable(pibot-rp ${PIBOT_RP_SOURCES})

target_include_directories(pibot-rp PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(pibot-rp PRIVATE
    Threads::Threads
    OpenSSL::SSL
    OpenSSL::Crypto
    PostgreSQL::PostgreSQL
    spdlog::spdlog
    oatpp
    ${OATPP_PQSQL_LIBRARIES}
    hiredis
    redis++
)
