#include "api/api_controller.h"

std::shared_ptr<ApiController> createApiController(std::shared_ptr<DbManager> db,
                                                   const std::string& apiKey,
                                                   std::function<bool()> healthy) {
    auto objectMapper = std::make_shared<oatpp::parser::json::mapping::ObjectMapper>();
    return std::make_shared<ApiController>(objectMapper, std::move(db), apiKey,
                                           std::move(healthy));
}
