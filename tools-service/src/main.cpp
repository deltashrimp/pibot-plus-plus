#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <memory>
#include <string>
#include <thread>

#include <oatpp/core/base/Environment.hpp>
#include <oatpp/network/Server.hpp>
#include <oatpp/network/tcp/server/ConnectionProvider.hpp>
#include <oatpp/parser/json/mapping/ObjectMapper.hpp>
#include <oatpp/web/server/HttpConnectionHandler.hpp>

#include "api/api_controller.h"
#include "logging/logger.h"
#include "service/git_cloner.h"

namespace {

constexpr int64_t kSweepIntervalSeconds = 30;

std::atomic<bool> g_running{true};
std::shared_ptr<oatpp::network::Server> g_server;
std::shared_ptr<tools::GitCloner> g_cloner;

void handleSignal(int) {
    g_running = false;
    if (g_server) {
        g_server->stop();
    }
}

std::string envOr(const char* name, const std::string& fallback = "") {
    const char* value = std::getenv(name);
    if (value == nullptr || *value == '\0') {
        return fallback;
    }
    return std::string(value);
}

int parseInt(const char* value, int fallback) {
    if (value == nullptr || *value == '\0') {
        return fallback;
    }
    char* end = nullptr;
    long parsed = std::strtol(value, &end, 10);
    if (end == value || *end != '\0') {
        return fallback;
    }
    return static_cast<int>(parsed);
}

long long parseLongLong(const char* value, long long fallback) {
    if (value == nullptr || *value == '\0') {
        return fallback;
    }
    char* end = nullptr;
    long long parsed = std::strtoll(value, &end, 10);
    if (end == value || *end != '\0') {
        return fallback;
    }
    return parsed;
}

spdlog::level::level_enum parseLogLevel(const std::string& level) {
    if (level == "debug") return spdlog::level::debug;
    if (level == "warn" || level == "warning") return spdlog::level::warn;
    if (level == "error") return spdlog::level::err;
    if (level == "critical") return spdlog::level::critical;
    return spdlog::level::info;
}

// Periodically removes archives whose cache TTL has expired.
void sweeperLoop() {
    while (g_running.load()) {
        std::this_thread::sleep_for(std::chrono::seconds(kSweepIntervalSeconds));
        if (g_cloner) {
            g_cloner->sweepExpired();
        }
    }
}

}  // namespace

int main() {
    std::signal(SIGINT, handleSignal);
    std::signal(SIGTERM, handleSignal);

    Logger::init(parseLogLevel(envOr("LOG_LEVEL", "info")));
    Logger::info("pibot-tools starting");

    const int port = parseInt(std::getenv("TOOLS_PORT"), 8084);
    const std::string apiKey = envOr("TOOLS_API_KEY");
    const std::string workdir = envOr("GCLONE_WORKDIR", "/data");
    const uint64_t maxBytes = static_cast<uint64_t>(
        parseLongLong(std::getenv("GCLONE_MAX_BYTES"), 20 * 1024 * 1024));
    const uint64_t ttlSeconds =
        static_cast<uint64_t>(parseLongLong(std::getenv("GCLONE_TTL_SECONDS"), 300));

    if (apiKey.empty()) {
        Logger::warn("TOOLS_API_KEY is not set; /gclone will reject all requests");
    }

    g_cloner = std::make_shared<tools::GitCloner>(workdir, maxBytes, ttlSeconds);
    Logger::info("git cloner ready: workdir=" + workdir +
                 " max_bytes=" + std::to_string(maxBytes) +
                 " ttl_seconds=" + std::to_string(ttlSeconds));

    std::thread sweeper(sweeperLoop);

    oatpp::base::Environment::init();
    auto objectMapper = std::make_shared<oatpp::parser::json::mapping::ObjectMapper>();
    auto router = oatpp::web::server::HttpRouter::createShared();
    router->addController(createToolsApiController(objectMapper, g_cloner, apiKey));

    auto handler = oatpp::web::server::HttpConnectionHandler::createShared(router);
    auto provider = oatpp::network::tcp::server::ConnectionProvider::createShared(
        {"0.0.0.0", static_cast<v_uint16>(port), oatpp::network::Address::IP_4});
    auto server = oatpp::network::Server::createShared(provider, handler);
    g_server = server;
    Logger::info("http server listening on port " + std::to_string(port));

    server->run(std::function<bool()>([]() { return g_running.load(); }));
    Logger::info("http server stopped");

    g_cloner->sweepExpired();
    sweeper.join();

    oatpp::base::Environment::destroy();
    Logger::info("pibot-tools stopped");
    return 0;
}
