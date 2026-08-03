#include "rp/rp_client.h"

#include <cstdlib>
#include <exception>
#include <string>
#include <utility>

#include <oatpp/core/macro/codegen.hpp>
#include <oatpp/network/Address.hpp>
#include <oatpp/network/tcp/client/ConnectionProvider.hpp>
#include <oatpp/parser/json/mapping/ObjectMapper.hpp>
#include <oatpp/web/client/HttpRequestExecutor.hpp>
#include <oatpp/web/protocol/http/incoming/Response.hpp>
#include <oatpp/web/protocol/http/outgoing/BufferBody.hpp>

#include "logging/logger.h"

namespace {

constexpr uint16_t kDefaultRpPort = 8081;

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

void parseBaseUrl(const std::string& url, std::string& host, uint16_t& port) {
    std::string rest = url;
    const std::string prefix = "http://";
    if (rest.rfind(prefix, 0) == 0) {
        rest = rest.substr(prefix.size());
    }
    size_t colon = rest.find(':');
    if (colon != std::string::npos) {
        host = rest.substr(0, colon);
        const std::string portStr = rest.substr(colon + 1);
        try {
            port = static_cast<uint16_t>(std::stoi(portStr));
        } catch (const std::exception&) {
            port = kDefaultRpPort;
        }
    } else {
        host = rest;
        port = kDefaultRpPort;
    }
    if (host.empty()) {
        host = "rp";
    }
}

}  // namespace

namespace rp {

RpClient::RpClient(std::string baseUrl, std::string apiKey)
    : baseUrl_(std::move(baseUrl)), apiKey_(std::move(apiKey)) {}

namespace {

std::shared_ptr<oatpp::web::protocol::http::incoming::Response> postJson(
    const std::string& baseUrl, const std::string& apiKey, const std::string& path,
    const oatpp::String& json) {
    std::string host;
    uint16_t port = kDefaultRpPort;
    parseBaseUrl(baseUrl, host, port);

    auto connectionProvider = oatpp::network::tcp::client::ConnectionProvider::createShared(
        {host.c_str(), port, oatpp::network::Address::IP_4});
    auto executor = oatpp::web::client::HttpRequestExecutor::createShared(connectionProvider);
    oatpp::web::protocol::http::Headers headers;
    headers.put("X-API-Key", oatpp::String(apiKey.c_str()));
    auto body = oatpp::web::protocol::http::outgoing::BufferBody::createShared(json, "application/json");
    return executor->execute("POST", path.c_str(), headers, body, nullptr);
}

std::string errorText(const std::shared_ptr<oatpp::web::protocol::http::incoming::Response>& response) {
    try {
        oatpp::String body = response->readBodyToString();
        if (body != nullptr && body->size() > 0) {
            return std::string(body->c_str());
        }
    } catch (const std::exception&) {
    }
    return "RP-сервис ответил с ошибкой (код " +
           std::to_string(response->getStatusCode()) + ")";
}

}  // namespace

MatchResult RpClient::match(int64_t chatId, int64_t userId, const std::string& text,
                            int64_t replyToUserId, const std::string& mention1,
                            const std::string& mention2) {
    MatchResult result;
    if (!enabled()) {
        return result;
    }
    try {
        auto objectMapper = std::make_shared<oatpp::parser::json::mapping::ObjectMapper>();
        auto req = MatchRequestDto::createShared();
        req->chat_id = chatId;
        req->user_id = userId;
        req->text = text.c_str();
        if (replyToUserId > 0) {
            req->reply_to_user_id = replyToUserId;
        }
        if (!mention1.empty()) {
            req->mention1 = mention1.c_str();
        }
        if (!mention2.empty()) {
            req->mention2 = mention2.c_str();
        }

        oatpp::String json = objectMapper->writeToString(req);
        auto response = postJson(baseUrl_, apiKey_, "/rp/match", json);
        if (response == nullptr) {
            return result;
        }
        if (response->getStatusCode() != 200) {
            Logger::warn("rp match failed (code=" +
                             std::to_string(response->getStatusCode()) + ")",
                         userId, chatId, "rp/match");
            return result;
        }
        oatpp::String body = response->readBodyToString();
        if (body == nullptr) {
            return result;
        }
        auto dto = objectMapper->readFromString<oatpp::Object<MatchResponseDto>>(body);
        if (dto == nullptr || dto->matched == nullptr) {
            return result;
        }
        result.matched = *dto->matched;
        if (result.matched) {
            result.response = dto->response != nullptr ? dto->response->c_str() : "";
            result.trigger = dto->trigger != nullptr ? dto->trigger->c_str() : "";
        }
    } catch (const std::exception& e) {
        Logger::warn(std::string("rp match error: ") + e.what(), userId, chatId, "rp/match");
    }
    return result;
}

CommandResult RpClient::executeCommand(const std::string& action, int64_t chatId,
                                       const std::string& trigger,
                                       const std::string& response) {
    CommandResult result;
    if (!enabled()) {
        result.message = "RP-сервис не настроен.";
        return result;
    }
    try {
        auto objectMapper = std::make_shared<oatpp::parser::json::mapping::ObjectMapper>();
        auto req = CommandRequestDto::createShared();
        req->action = action.c_str();
        req->chat_id = chatId;
        if (!trigger.empty()) {
            req->trigger = trigger.c_str();
        }
        if (!response.empty()) {
            req->response = response.c_str();
        }
        oatpp::String json = objectMapper->writeToString(req);
        auto responsePtr = postJson(baseUrl_, apiKey_, "/rp/command", json);
        if (responsePtr == nullptr) {
            result.message = "RP-сервис недоступен.";
            return result;
        }
        oatpp::String body = responsePtr->readBodyToString();
        if (body == nullptr) {
            result.message = "RP-сервис вернул пустой ответ.";
            return result;
        }
        try {
            auto dto = objectMapper->readFromString<oatpp::Object<CommandResponseDto>>(body);
            if (dto == nullptr) {
                result.message = std::string(body->c_str());
                return result;
            }
            if (dto->ok != nullptr) {
                result.ok = *dto->ok;
            }
            result.message = dto->message != nullptr ? dto->message->c_str() : "";
        } catch (const std::exception&) {
            // Non-JSON body: surface the plain-text error from the service.
            result.message = std::string(body->c_str());
        }
    } catch (const std::exception& e) {
        result.message = "Ошибка обращения к RP-сервису: " + std::string(e.what());
    }
    return result;
}

CommandResult RpClient::addCommand(int64_t chatId, const std::string& trigger,
                                   const std::string& response) {
    return executeCommand("add", chatId, trigger, response);
}

CommandResult RpClient::removeCommand(int64_t chatId, const std::string& trigger) {
    return executeCommand("remove", chatId, trigger, "");
}

CommandResult RpClient::editCommand(int64_t chatId, const std::string& trigger,
                                    const std::string& response) {
    return executeCommand("edit", chatId, trigger, response);
}

std::unordered_map<std::string, std::string> RpClient::listCommands(int64_t chatId) {
    std::unordered_map<std::string, std::string> result;
    if (!enabled()) {
        return result;
    }
    try {
        auto objectMapper = std::make_shared<oatpp::parser::json::mapping::ObjectMapper>();
        auto req = CommandRequestDto::createShared();
        req->action = "list";
        req->chat_id = chatId;
        oatpp::String json = objectMapper->writeToString(req);
        auto response = postJson(baseUrl_, apiKey_, "/rp/command", json);
        if (response == nullptr || response->getStatusCode() != 200) {
            return result;
        }
        oatpp::String body = response->readBodyToString();
        if (body == nullptr) {
            return result;
        }
        auto dto = objectMapper->readFromString<oatpp::Object<CommandListDto>>(body);
        if (dto == nullptr || dto->commands == nullptr) {
            return result;
        }
        for (auto it = dto->commands->begin(); it != dto->commands->end(); ++it) {
            result[it->first->c_str()] = it->second != nullptr ? it->second->c_str() : "";
        }
    } catch (const std::exception& e) {
        Logger::warn(std::string("rp list error: ") + e.what(), 0, chatId, "rplist");
    }
    return result;
}

}  // namespace rp
