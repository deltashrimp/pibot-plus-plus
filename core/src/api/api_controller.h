#ifndef PIBOT_API_CONTROLLER_H
#define PIBOT_API_CONTROLLER_H

#include <cstring>
#include <functional>
#include <memory>
#include <string>

#include <oatpp/core/data/mapping/type/Vector.hpp>
#include <oatpp/core/macro/codegen.hpp>
#include <oatpp/parser/json/mapping/ObjectMapper.hpp>
#include <oatpp/web/server/api/ApiController.hpp>

#include "database/db_manager.h"

#include OATPP_CODEGEN_BEGIN(DTO)

class HealthDto : public oatpp::DTO {
    DTO_INIT(HealthDto, DTO)
    DTO_FIELD(String, status);
};

class GlobalBansDto : public oatpp::DTO {
    DTO_INIT(GlobalBansDto, DTO)
    DTO_FIELD(Vector<Int64>, banned_user_ids);
};

class RankDto : public oatpp::DTO {
    DTO_INIT(RankDto, DTO)
    DTO_FIELD(Int64, chat_id);
    DTO_FIELD(Int64, user_id);
    DTO_FIELD(Int32, rank);
};

class MuteDto : public oatpp::DTO {
    DTO_INIT(MuteDto, DTO)
    DTO_FIELD(Int64, chat_id);
    DTO_FIELD(Int64, user_id);
    DTO_FIELD(Boolean, muted);
    DTO_FIELD(Int64, until);
};

#include OATPP_CODEGEN_END(DTO)

#include OATPP_CODEGEN_BEGIN(ApiController)

class ApiController : public oatpp::web::server::api::ApiController {
public:
    ApiController(const std::shared_ptr<oatpp::data::mapping::ObjectMapper>& objectMapper,
                  std::shared_ptr<DbManager> db, std::string apiKey,
                  std::function<bool()> healthy)
        : oatpp::web::server::api::ApiController(objectMapper),
          db_(std::move(db)),
          apiKey_(std::move(apiKey)),
          healthy_(std::move(healthy)) {}

    ENDPOINT("GET", "/health", health, REQUEST(std::shared_ptr<IncomingRequest>, request)) {
        (void)request;
        auto dto = HealthDto::createShared();
        if (healthy_ && healthy_()) {
            dto->status = "ok";
            return createDtoResponse(Status::CODE_200, dto);
        }
        dto->status = "unhealthy";
        return createDtoResponse(Status::CODE_503, dto);
    }

    ENDPOINT("GET", "/config/global_bans", getGlobalBans,
             REQUEST(std::shared_ptr<IncomingRequest>, request)) {
        if (!authorized(request)) {
            return unauthorized();
        }
        auto dto = GlobalBansDto::createShared();
        dto->banned_user_ids = oatpp::Vector<oatpp::Int64>::createShared();
        for (int64_t id : db_->getGlobalBans()) {
            dto->banned_user_ids->push_back(id);
        }
        return createDtoResponse(Status::CODE_200, dto);
    }

    ENDPOINT("GET", "/config/chat/{chatId}/rank/{userId}", getRank,
             PATH(Int64, chatId, "chatId"), PATH(Int64, userId, "userId"),
             REQUEST(std::shared_ptr<IncomingRequest>, request)) {
        if (!authorized(request)) {
            return unauthorized();
        }
        auto dto = RankDto::createShared();
        dto->chat_id = chatId;
        dto->user_id = userId;
        dto->rank = db_->getChatRank(chatId, userId);
        return createDtoResponse(Status::CODE_200, dto);
    }

    ENDPOINT("GET", "/config/chat/{chatId}/mute/{userId}", getMute,
             PATH(Int64, chatId, "chatId"), PATH(Int64, userId, "userId"),
             REQUEST(std::shared_ptr<IncomingRequest>, request)) {
        if (!authorized(request)) {
            return unauthorized();
        }
        auto dto = MuteDto::createShared();
        dto->chat_id = chatId;
        dto->user_id = userId;
        MuteInfo info = db_->getMute(chatId, userId);
        dto->muted = info.muted;
        dto->until = info.until;
        return createDtoResponse(Status::CODE_200, dto);
    }

private:
    bool authorized(const std::shared_ptr<IncomingRequest>& request) const {
        auto key = request->getHeader("X-API-Key");
        if (key == nullptr) {
            return false;
        }
        return std::strcmp(key->c_str(), apiKey_.c_str()) == 0;
    }

    std::shared_ptr<OutgoingResponse> unauthorized() const {
        return createResponse(Status::CODE_401, "Unauthorized");
    }

    std::shared_ptr<DbManager> db_;
    std::string apiKey_;
    std::function<bool()> healthy_;
};

#include OATPP_CODEGEN_END(ApiController)

std::shared_ptr<ApiController> createApiController(std::shared_ptr<DbManager> db,
                                                   const std::string& apiKey,
                                                   std::function<bool()> healthy);

#endif
