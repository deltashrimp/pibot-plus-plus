#include <memory>

#include <oatpp/core/macro/codegen.hpp>
#include <oatpp/network/Server.hpp>
#include <oatpp/network/tcp/server/ConnectionProvider.hpp>
#include <oatpp/parser/json/mapping/ObjectMapper.hpp>
#include <oatpp/web/server/HttpConnectionHandler.hpp>
#include <oatpp/web/server/api/ApiController.hpp>

#include "auto_mod_engine.h"

#include OATPP_CODEGEN_BEGIN(ApiController)

class AutoModController : public oatpp::web::server::api::ApiController {
public:
    AutoModController(const std::shared_ptr<oatpp::data::mapping::ObjectMapper>& objectMapper)
        : oatpp::web::server::api::ApiController(objectMapper) {}

    ENDPOINT("GET", "/", root) {
        return createResponse(Status::CODE_200, "pibot-auto-mod: skeleton");
    }

    ENDPOINT("GET", "/health", health) {
        return createResponse(Status::CODE_200, "ok");
    }
};

#include OATPP_CODEGEN_END(ApiController)

int main() {
    oatpp::base::Environment::init();
    auto objectMapper = std::make_shared<oatpp::parser::json::mapping::ObjectMapper>();
    auto router = oatpp::web::server::HttpRouter::createShared();
    router->addController(std::make_shared<AutoModController>(objectMapper));
    auto handler = oatpp::web::server::HttpConnectionHandler::createShared(router);
    auto provider = oatpp::network::tcp::server::ConnectionProvider::createShared(
        {"0.0.0.0", 8083, oatpp::network::Address::IP_4});
    oatpp::network::Server server(provider, handler);
    server.run();
    oatpp::base::Environment::destroy();
    return 0;
}
