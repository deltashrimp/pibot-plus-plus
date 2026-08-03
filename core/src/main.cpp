#include <atomic>
#include <csignal>
#include <cstdlib>
#include <memory>
#include <string>
#include <thread>

#include <oatpp/core/base/Environment.hpp>
#include <oatpp/network/Server.hpp>
#include <oatpp/network/tcp/server/ConnectionProvider.hpp>
#include <oatpp/web/server/HttpConnectionHandler.hpp>

#include "api/api_controller.h"
#include "commands/moderation_commands.h"
#include "database/db_manager.h"
#include "logging/logger.h"
#include "rp/rp_client.h"
#include "tdlib/tdlib_client.h"
#include "utils/helpers.h"

namespace {

std::atomic<bool> g_running{true};
std::shared_ptr<oatpp::network::Server> g_server;
std::shared_ptr<TdlibClient> g_tdlib;

void handleSignal(int) {
    g_running = false;
    if (g_server) {
        g_server->stop();
    }
    if (g_tdlib) {
        g_tdlib->stop();
    }
}

std::string envOr(const char* name, const std::string& fallback = "") {
    const char* value = std::getenv(name);
    if (value == nullptr || *value == '\0') {
        return fallback;
    }
    return std::string(value);
}

}  // namespace

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;

    std::signal(SIGINT, handleSignal);
    std::signal(SIGTERM, handleSignal);

    Logger::init(helpers::parseLogLevel(envOr("LOG_LEVEL", "info")));
    Logger::info("pibot-core starting");

    DbConfig dbConfig;
    dbConfig.host = envOr("POSTGRES_HOST", "localhost");
    dbConfig.port = envOr("POSTGRES_PORT", "5432");
    dbConfig.dbname = envOr("POSTGRES_DB", "pibot");
    dbConfig.user = envOr("POSTGRES_USER", "pibot");
    dbConfig.password = envOr("POSTGRES_PASSWORD", "");

    auto db = DbManager::create(dbConfig);
    db->initSchema();
    Logger::info("database schema ready");

    std::string token = envOr("TELEGRAM_TOKEN");
    int apiId = helpers::parseInt(std::getenv("TELEGRAM_API_ID"), 0);
    std::string apiHash = envOr("TELEGRAM_API_HASH");
    int corePort = helpers::parseInt(std::getenv("CORE_PORT"), 8080);
    std::string apiKey = envOr("CORE_API_KEY");

    auto tdlib = std::make_shared<TdlibClient>();
    g_tdlib = tdlib;
    auto rpClient = std::make_shared<rp::RpClient>(
        envOr("RP_SERVICE_URL", "http://rp:8081"), envOr("RP_API_KEY"));
    auto commands = std::make_shared<ModerationCommands>(tdlib, db, rpClient);
    tdlib->setMessageHandler(
        [commands](td::td_api::object_ptr<td::td_api::message> message) {
            commands->handleMessage(std::move(message));
        });
    tdlib->init(token, apiId, apiHash);
    std::thread tdThread([tdlib] { tdlib->run(); });
    Logger::info("tdlib event loop started");

    oatpp::base::Environment::init();
    auto router = oatpp::web::server::HttpRouter::createShared();
    auto healthy = [db, tdlib]() { return db->ping() && tdlib->authorized(); };
    router->addController(createApiController(db, apiKey, std::move(healthy)));
    auto handler = oatpp::web::server::HttpConnectionHandler::createShared(router);
    auto provider = oatpp::network::tcp::server::ConnectionProvider::createShared(
        {"0.0.0.0", static_cast<v_uint16>(corePort), oatpp::network::Address::IP_4});
    auto server = oatpp::network::Server::createShared(provider, handler);
    g_server = server;
    Logger::info("api server listening on port " + std::to_string(corePort));

    server->run(std::function<bool()>([]() { return g_running.load(); }));
    Logger::info("api server stopped");

    tdlib->stop();
    tdThread.join();
    Logger::info("tdlib thread stopped");

    oatpp::base::Environment::destroy();
    Logger::info("pibot-core stopped");
    return 0;
}
