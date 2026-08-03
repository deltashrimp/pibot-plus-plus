#ifndef PIBOT_RP_API_CONTROLLER_H
#define PIBOT_RP_API_CONTROLLER_H

#include <cstring>
#include <memory>
#include <string>

#include <oatpp/core/data/mapping/type/UnorderedMap.hpp>
#include <oatpp/core/macro/codegen.hpp>
#include <oatpp/parser/json/mapping/ObjectMapper.hpp>
#include <oatpp/web/server/api/ApiController.hpp>

#include "logging/logger.h"
#include "service/rp_service.h"

#include OATPP_CODEGEN_BEGIN(DTO)

class MatchRequestDto : public oatpp::DTO {
    DTO_INIT(MatchRequestDto, DTO)
    DTO_FIELD(Int64, chat_id);
    DTO_FIELD(Int64, user_id);
    DTO_FIELD(String, text);
    DTO_FIELD(Int64, reply_to_user_id);
    DTO_FIELD(String, mention1);
    DTO_FIELD(String, mention2);
};

class MatchResponseDto : public oatpp::DTO {
    DTO_INIT(MatchResponseDto, DTO)
    DTO_FIELD(Boolean, matched);
    DTO_FIELD(String, response);
    DTO_FIELD(String, trigger);
};

class CommandRequestDto : public oatpp::DTO {
    DTO_INIT(CommandRequestDto, DTO)
    DTO_FIELD(String, action);
    DTO_FIELD(Int64, chat_id);
    DTO_FIELD(String, trigger);
    DTO_FIELD(String, response);
};

class CommandResponseDto : public oatpp::DTO {
    DTO_INIT(CommandResponseDto, DTO)
    DTO_FIELD(Boolean, ok);
    DTO_FIELD(String, message);
};

class CommandListDto : public oatpp::DTO {
    DTO_INIT(CommandListDto, DTO)
    DTO_FIELD(UnorderedFields<String>, commands);
};

#include OATPP_CODEGEN_END(DTO)

#include OATPP_CODEGEN_BEGIN(ApiController)

class RpApiController : public oatpp::web::server::api::ApiController {
public:
    RpApiController(const std::shared_ptr<oatpp::data::mapping::ObjectMapper>& objectMapper,
                    std::shared_ptr<rp::RpService> service, std::string apiKey)
        : oatpp::web::server::api::ApiController(objectMapper),
          objectMapper_(objectMapper),
          service_(std::move(service)),
          apiKey_(std::move(apiKey)) {}

    ENDPOINT("GET", "/health", health) {
        return createResponse(Status::CODE_200, "{\"status\":\"ok\"}");
    }

    ENDPOINT("POST", "/rp/match", match,
             REQUEST(std::shared_ptr<IncomingRequest>, request)) {
        if (!authorized(request)) {
            return unauthorized();
        }
        MatchRequestDto::Wrapper dto;
        try {
            dto = request->readBodyToDto<MatchRequestDto::Wrapper>(objectMapper_);
        } catch (const std::exception&) {
            return badRequest("Некорректный JSON");
        }
        if (dto == nullptr || dto->chat_id == nullptr || dto->user_id == nullptr ||
            dto->text == nullptr) {
            return badRequest("Отсутствуют обязательные поля: chat_id, user_id, text");
        }

        const int64_t replyTo =
            dto->reply_to_user_id != nullptr ? *dto->reply_to_user_id : 0;
        const std::string mention1 =
            dto->mention1 != nullptr ? dto->mention1->c_str() : "";
        const std::string mention2 =
            dto->mention2 != nullptr ? dto->mention2->c_str() : "";

        auto result = service_->match(*dto->chat_id, *dto->user_id, dto->text->c_str(),
                                      replyTo, mention1, mention2);

        auto response = MatchResponseDto::createShared();
        response->matched = result.matched;
        if (result.matched) {
            response->response = result.response.c_str();
            response->trigger = result.trigger.c_str();
        }
        Logger::event("match", *dto->chat_id, *dto->user_id, result.trigger,
                      result.matched ? "matched" : "no_match");
        return createDtoResponse(Status::CODE_200, response);
    }

    ENDPOINT("POST", "/rp/command", manageCommand,
             REQUEST(std::shared_ptr<IncomingRequest>, request)) {
        if (!authorized(request)) {
            return unauthorized();
        }
        CommandRequestDto::Wrapper dto;
        try {
            dto = request->readBodyToDto<CommandRequestDto::Wrapper>(objectMapper_);
        } catch (const std::exception&) {
            return badRequest("Некорректный JSON");
        }
        if (dto == nullptr || dto->action == nullptr || dto->chat_id == nullptr) {
            return badRequest("Отсутствуют обязательные поля: action, chat_id");
        }

        const std::string action = dto->action->c_str();
        const std::string trigger =
            dto->trigger != nullptr ? dto->trigger->c_str() : "";
        const std::string response =
            dto->response != nullptr ? dto->response->c_str() : "";
        const int64_t chatId = *dto->chat_id;

        if (action == "list") {
            auto commands = service_->listCommands(chatId);
            auto listDto = CommandListDto::createShared();
            listDto->commands = oatpp::data::mapping::type::UnorderedMap<
                oatpp::String, oatpp::String>::createShared();
            for (const auto& pair : commands) {
                listDto->commands[pair.first.c_str()] = pair.second.c_str();
            }
            Logger::event("command_list", chatId, 0, "",
                          std::to_string(commands.size()) + " commands");
            return createDtoResponse(Status::CODE_200, listDto);
        }

        rp::CommandResult result;
        if (action == "add") {
            result = service_->addCommand(chatId, trigger, response);
        } else if (action == "remove") {
            result = service_->removeCommand(chatId, trigger);
        } else if (action == "edit") {
            result = service_->editCommand(chatId, trigger, response);
        } else {
            return badRequest("Неизвестное действие: " + action +
                              ". Доступно: add, remove, edit, list");
        }

        Logger::event("command_" + action, chatId, 0, trigger,
                      result.ok ? "ok" : result.message,
                      result.ok ? spdlog::level::info : spdlog::level::warn);

        auto cmdResponse = CommandResponseDto::createShared();
        cmdResponse->ok = result.ok;
        cmdResponse->message = result.message.c_str();
        return createDtoResponse(result.ok ? Status::CODE_200 : Status::CODE_400,
                                 cmdResponse);
    }

private:
    bool authorized(const std::shared_ptr<IncomingRequest>& request) const {
        const oatpp::String key = request->getHeader("X-API-Key");
        if (key == nullptr) {
            return false;
        }
        return std::strcmp(key->c_str(), apiKey_.c_str()) == 0;
    }

    std::shared_ptr<OutgoingResponse> unauthorized() const {
        return createResponse(Status::CODE_401, "Unauthorized");
    }

    std::shared_ptr<OutgoingResponse> badRequest(const std::string& message) const {
        return createResponse(Status::CODE_400, message.c_str());
    }

    std::shared_ptr<oatpp::data::mapping::ObjectMapper> objectMapper_;
    std::shared_ptr<rp::RpService> service_;
    std::string apiKey_;
};

#include OATPP_CODEGEN_END(ApiController)

std::shared_ptr<RpApiController> createRpApiController(
    const std::shared_ptr<oatpp::data::mapping::ObjectMapper>& objectMapper,
    std::shared_ptr<rp::RpService> service, std::string apiKey);

#endif  // PIBOT_RP_API_CONTROLLER_H
