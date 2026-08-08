#include "ai/ai_client.h"

#include <cstdlib>
#include <exception>
#include <string>
#include <utility>

#include <oatpp/core/macro/codegen.hpp>
#include <oatpp/network/Address.hpp>
#include <oatpp/network/tcp/client/ConnectionProvider.hpp>
#include <oatpp/parser/json/mapping/ObjectMapper.hpp>
#include <oatpp/web/client/HttpRequestExecutor.hpp>
#include <oatpp/web/protocol/http/Http.hpp>
#include <oatpp/web/protocol/http/incoming/Response.hpp>
#include <oatpp/web/protocol/http/outgoing/BufferBody.hpp>

#include "logging/logger.h"

namespace {

constexpr uint16_t kDefaultAiPort = 8082;

#include OATPP_CODEGEN_BEGIN(DTO)

class AskRequestDto : public oatpp::DTO {
    DTO_INIT(AskRequestDto, DTO)
    DTO_FIELD(String, message);
};

class AskResponseDto : public oatpp::DTO {
    DTO_INIT(AskResponseDto, DTO)
    DTO_FIELD(String, response);
};

#include OATPP_CODEGEN_END(DTO)

void parseBaseUrl(const std::string& url, std::string& host, uint16_t& port) {
    std::string rest = url;
    const std::string prefix = "http://";
    if (rest.rfind(prefix, 0) == 0) {
        rest = rest.substr(prefix.size());
    }
    const size_t colon = rest.find(':');
    if (colon != std::string::npos) {
        host = rest.substr(0, colon);
        const std::string portStr = rest.substr(colon + 1);
        try {
            port = static_cast<uint16_t>(std::stoi(portStr));
        } catch (const std::exception&) {
            port = kDefaultAiPort;
        }
    } else {
        host = rest;
        port = kDefaultAiPort;
    }
    if (host.empty()) {
        host = "ai";
    }
}

}  // namespace

namespace ai {

AiClient::AiClient(std::string baseUrl, std::string apiKey)
    : baseUrl_(std::move(baseUrl)), apiKey_(std::move(apiKey)) {}

AskResult AiClient::ask(const std::string& message) {
    AskResult result;
    if (!enabled()) {
        result.error = "AI-сервис не настроен.";
        return result;
    }
    try {
        auto objectMapper = std::make_shared<oatpp::parser::json::mapping::ObjectMapper>();
        auto req = AskRequestDto::createShared();
        req->message = message.c_str();
        oatpp::String json = objectMapper->writeToString(req);

        std::string host;
        uint16_t port = kDefaultAiPort;
        parseBaseUrl(baseUrl_, host, port);

        auto connectionProvider = oatpp::network::tcp::client::ConnectionProvider::createShared(
            {host.c_str(), port, oatpp::network::Address::IP_4});
        auto executor = oatpp::web::client::HttpRequestExecutor::createShared(connectionProvider);
        oatpp::web::protocol::http::Headers headers;
        headers.put("X-API-Key", oatpp::String(apiKey_.c_str()));
        auto body = oatpp::web::protocol::http::outgoing::BufferBody::createShared(
            json, "application/json");
        auto response = executor->execute("POST", "ai/ask", headers, body, nullptr);
        if (response == nullptr) {
            result.error = "AI-сервис недоступен.";
            return result;
        }

        if (response->getStatusCode() == 200) {
            const oatpp::String bodyStr = response->readBodyToString();
            if (bodyStr != nullptr && bodyStr->size() > 0) {
                auto resp = objectMapper->readFromString<oatpp::Object<AskResponseDto>>(bodyStr);
                if (resp != nullptr && resp->response != nullptr) {
                    result.response = resp->response->c_str();
                    result.ok = true;
                    return result;
                }
            }
            result.error = "AI-сервис вернул пустой ответ.";
            return result;
        }

        try {
            const oatpp::String bodyStr = response->readBodyToString();
            if (bodyStr != nullptr && bodyStr->size() > 0) {
                result.error = bodyStr->c_str();
            }
        } catch (const std::exception&) {
        }
        if (result.error.empty()) {
            result.error = "AI-сервис ответил с ошибкой (код " +
                           std::to_string(response->getStatusCode()) + ")";
        }
    } catch (const std::exception& e) {
        result.error = "Ошибка обращения к AI-сервису: " + std::string(e.what());
    }
    return result;
}

}  // namespace ai
