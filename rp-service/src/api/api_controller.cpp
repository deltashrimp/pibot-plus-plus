#include "api/api_controller.h"

std::shared_ptr<RpApiController> createRpApiController(
    const std::shared_ptr<oatpp::data::mapping::ObjectMapper>& objectMapper,
    std::shared_ptr<rp::RpService> service, std::string apiKey) {
    return std::make_shared<RpApiController>(objectMapper, std::move(service),
                                             std::move(apiKey));
}
