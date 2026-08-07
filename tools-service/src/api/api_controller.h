#ifndef PIBOT_TOOLS_API_CONTROLLER_H
#define PIBOT_TOOLS_API_CONTROLLER_H

#include <cstring>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include <oatpp/core/macro/codegen.hpp>
#include <oatpp/parser/json/mapping/ObjectMapper.hpp>
#include <oatpp/web/server/api/ApiController.hpp>

#include "logging/logger.h"
#include "service/git_cloner.h"

#include OATPP_CODEGEN_BEGIN(DTO)

class GcloneRequestDto : public oatpp::DTO {
    DTO_INIT(GcloneRequestDto, DTO)
    DTO_FIELD(String, url);
};

#include OATPP_CODEGEN_END(DTO)

#include OATPP_CODEGEN_BEGIN(ApiController)

class ToolsApiController : public oatpp::web::server::api::ApiController {
public:
    ToolsApiController(const std::shared_ptr<oatpp::data::mapping::ObjectMapper>& objectMapper,
                       std::shared_ptr<tools::GitCloner> cloner, std::string apiKey)
        : oatpp::web::server::api::ApiController(objectMapper),
          objectMapper_(objectMapper),
          cloner_(std::move(cloner)),
          apiKey_(std::move(apiKey)) {}

    ENDPOINT("GET", "/health", health) {
        return createResponse(Status::CODE_200, "{\"status\":\"ok\"}");
    }

    // Clones a repository and returns the .zip archive as the response body.
    // On success: 200, Content-Type: application/zip, X-Archive-Name: <name>.zip.
    // On failure: 4xx/5xx with a plain-text error message.
    ENDPOINT("POST", "/gclone", gclone, REQUEST(std::shared_ptr<IncomingRequest>, request)) {
        if (!authorized(request)) {
            return unauthorized();
        }
        GcloneRequestDto::Wrapper dto;
        try {
            dto = request->readBodyToDto<GcloneRequestDto::Wrapper>(objectMapper_);
        } catch (const std::exception& e) {
            const std::string detail = e.what();
            return badRequest(detail.empty() ? "Некорректный JSON"
                                             : "Некорректный JSON: " + detail);
        } catch (...) {
            return badRequest("Некорректный JSON");
        }
        if (dto == nullptr || dto->url == nullptr) {
            return badRequest("Отсутствует обязательное поле: url");
        }

        const std::string url = dto->url->c_str();
        Logger::event("gclone_request", url);

        const tools::CloneResult result = cloner_->clone(url);
        if (!result.ok) {
            Logger::event("gclone_failed", url + " - " + result.error,
                          spdlog::level::warn);
            return badRequest(result.error);
        }

        std::vector<char> bytes;
        if (!readFile(result.archive_path, bytes)) {
            Logger::error("failed to read archive: " + result.archive_path);
            return error("Не удалось прочитать архив.");
        }

        auto response = createResponse(
            Status::CODE_200,
            oatpp::String(bytes.data(), static_cast<v_buff_size>(bytes.size())));
        response->putHeader("Content-Type", "application/zip");
        response->putHeader("X-Archive-Name", result.archive_name.c_str());
        Logger::event("gclone_ok", url + " -> " + result.archive_name +
                                       " (" + std::to_string(bytes.size()) + " bytes)");
        return response;
    }

private:
    bool readFile(const std::string& path, std::vector<char>& out) const {
        std::ifstream file(path, std::ios::binary);
        if (!file) {
            return false;
        }
        file.seekg(0, std::ios::end);
        const std::streamoff size = file.tellg();
        if (size <= 0) {
            return false;
        }
        file.seekg(0, std::ios::beg);
        out.resize(static_cast<size_t>(size));
        file.read(out.data(), size);
        return file.good() || file.eof();
    }

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

    std::shared_ptr<OutgoingResponse> error(const std::string& message) const {
        return createResponse(Status::CODE_500, message.c_str());
    }

    std::shared_ptr<oatpp::data::mapping::ObjectMapper> objectMapper_;
    std::shared_ptr<tools::GitCloner> cloner_;
    std::string apiKey_;
};

#include OATPP_CODEGEN_END(ApiController)

std::shared_ptr<ToolsApiController> createToolsApiController(
    const std::shared_ptr<oatpp::data::mapping::ObjectMapper>& objectMapper,
    std::shared_ptr<tools::GitCloner> cloner, std::string apiKey);

#endif  // PIBOT_TOOLS_API_CONTROLLER_H
