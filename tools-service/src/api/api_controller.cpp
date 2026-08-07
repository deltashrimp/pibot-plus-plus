#include "api/api_controller.h"

std::shared_ptr<ToolsApiController> createToolsApiController(
    const std::shared_ptr<oatpp::data::mapping::ObjectMapper>& objectMapper,
    std::shared_ptr<tools::GitCloner> cloner, std::string apiKey) {
    return std::make_shared<ToolsApiController>(objectMapper, std::move(cloner),
                                                std::move(apiKey));
}
