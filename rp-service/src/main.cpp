#include <atomic>
#include <csignal>
#include <cstdlib>
#include <memory>
#include <string>

#include <oatpp/core/base/Environment.hpp>
#include <oatpp/network/Server.hpp>
#include <oatpp/network/tcp/server/ConnectionProvider.hpp>
#include <oatpp/parser/json/mapping/ObjectMapper.hpp>
#include <oatpp/web/server/HttpConnectionHandler.hpp>

#include "api/api_controller.h"
#include "logging/logger.h"
#include "service/rp_service.h"
#include "storage/redis_storage.h"

namespace {

std::atomic<bool> g_running{true};
std::shared_ptr<oatpp::network::Server> g_server;

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

spdlog::level::level_enum parseLogLevel(const std::string& level) {
    if (level == "debug") return spdlog::level::debug;
    if (level == "warn" || level == "warning") return spdlog::level::warn;
    if (level == "error") return spdlog::level::err;
    if (level == "critical") return spdlog::level::critical;
    return spdlog::level::info;
}

}  // namespace

int main() {
    std::signal(SIGINT, handleSignal);
    std::signal(SIGTERM, handleSignal);

    Logger::init(parseLogLevel(envOr("LOG_LEVEL", "info")));
    Logger::info("pibot-rp starting");

    const std::string redisHost = envOr("REDIS_HOST", "redis");
    const int redisPort = parseInt(std::getenv("REDIS_PORT"), 6379);
    const int port = parseInt(std::getenv("RP_PORT"), 8081);
    const std::string apiKey = envOr("RP_API_KEY");
    const std::string phrasesFile = envOr("RP_PHRASES_FILE", "/app/rp_phrases.json");

    if (apiKey.empty()) {
        Logger::warn("RP_API_KEY is not set; /rp endpoints will reject all requests");
    }

    auto storage = std::make_shared<rp::RedisStorage>(
        redisHost, static_cast<uint16_t>(redisPort));
    if (!storage->ping()) {
        Logger::error("cannot connect to redis at " + redisHost + ":" +
                      std::to_string(redisPort));
        return 1;
    }
    Logger::info("redis connected at " + redisHost + ":" + std::to_string(redisPort));

    auto service = std::make_shared<rp::RpService>(storage);
    if (service->loadPredefined(phrasesFile)) {
        Logger::info("predefined commands loaded from " + phrasesFile);
    } else {
        Logger::warn("predefined commands not loaded; only user commands will work");
    }

    oatpp::base::Environment::init();
    auto objectMapper = std::make_shared<oatpp::parser::json::mapping::ObjectMapper>();
    auto router = oatpp::web::server::HttpRouter::createShared();
    router->addController(createRpApiController(objectMapper, service, apiKey));

    auto handler = oatpp::web::server::HttpConnectionHandler::createShared(router);
    auto provider = oatpp::network::tcp::server::ConnectionProvider::createShared(
        {"0.0.0.0", static_cast<v_uint16>(port), oatpp::network::Address::IP_4});
    auto server = oatpp::network::Server::createShared(provider, handler);
    g_server = server;
    Logger::info("http server listening on port " + std::to_string(port));

    server->run(std::function<bool()>([]() { return g_running.load(); }));
    Logger::info("http server stopped");

    oatpp::base::Environment::destroy();
    Logger::info("pibot-rp stopped");
    return 0;
}
