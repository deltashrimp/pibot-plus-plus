#include "tools/tools_client.h"

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

constexpr uint16_t kDefaultToolsPort = 8084;

#include OATPP_CODEGEN_BEGIN(DTO)

class GcloneRequestDto : public oatpp::DTO {
    DTO_INIT(GcloneRequestDto, DTO)
    DTO_FIELD(String, url);
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
            port = kDefaultToolsPort;
        }
    } else {
        host = rest;
        port = kDefaultToolsPort;
    }
    if (host.empty()) {
        host = "tools";
    }
}

}  // namespace

namespace tools {

ToolsClient::ToolsClient(std::string baseUrl, std::string apiKey)
    : baseUrl_(std::move(baseUrl)), apiKey_(std::move(apiKey)) {}

namespace {

std::shared_ptr<oatpp::web::protocol::http::incoming::Response> postJson(
    const std::string& baseUrl, const std::string& apiKey, const std::string& path,
    const oatpp::String& json) {
    std::string host;
    uint16_t port = kDefaultToolsPort;
    parseBaseUrl(baseUrl, host, port);

    auto connectionProvider = oatpp::network::tcp::client::ConnectionProvider::createShared(
        {host.c_str(), port, oatpp::network::Address::IP_4});
    auto executor = oatpp::web::client::HttpRequestExecutor::createShared(connectionProvider);
    oatpp::web::protocol::http::Headers headers;
    headers.put("X-API-Key", oatpp::String(apiKey.c_str()));
    auto body = oatpp::web::protocol::http::outgoing::BufferBody::createShared(json, "application/json");
    return executor->execute("POST", path.c_str(), headers, body, nullptr);
}

}  // namespace

CloneResult ToolsClient::clone(const std::string& url) {
    CloneResult result;
    if (!enabled()) {
        result.error = "tools-service не настроен.";
        return result;
    }
    try {
        auto objectMapper = std::make_shared<oatpp::parser::json::mapping::ObjectMapper>();
        auto req = GcloneRequestDto::createShared();
        req->url = url.c_str();
        oatpp::String json = objectMapper->writeToString(req);

        auto response = postJson(baseUrl_, apiKey_, "/gclone", json);
        if (response == nullptr) {
            result.error = "tools-service недоступен.";
            return result;
        }

        if (response->getStatusCode() == 200) {
            const oatpp::String nameHeader = response->getHeader("X-Archive-Name");
            if (nameHeader != nullptr && !nameHeader->empty()) {
                result.archive_name = nameHeader->c_str();
            }
            const oatpp::String body = response->readBodyToString();
            if (body != nullptr && body->size() > 0) {
                result.archive_bytes.assign(body->data(), body->data() + body->size());
            }
            if (result.archive_bytes.empty()) {
                result.error = "tools-service вернул пустой архив.";
                return result;
            }
            result.ok = true;
            return result;
        }

        try {
            const oatpp::String body = response->readBodyToString();
            if (body != nullptr && body->size() > 0) {
                result.error = body->c_str();
            }
        } catch (const std::exception&) {
        }
        if (result.error.empty()) {
            result.error = "tools-service ответил с ошибкой (код " +
                           std::to_string(response->getStatusCode()) + ")";
        }
    } catch (const std::exception& e) {
        result.error = "Ошибка обращения к tools-service: " + std::string(e.what());
    }
    return result;
}

}  // namespace tools
