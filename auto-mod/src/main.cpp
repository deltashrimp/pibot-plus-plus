#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <memory>
#include <string>

#include <oatpp/core/base/Environment.hpp>
#include <oatpp/core/macro/codegen.hpp>
#include <oatpp/network/Server.hpp>
#include <oatpp/network/tcp/server/ConnectionProvider.hpp>
#include <oatpp/parser/json/mapping/ObjectMapper.hpp>
#include <oatpp/web/server/HttpConnectionHandler.hpp>
#include <oatpp/web/server/api/ApiController.hpp>

#include "automoderator.hpp"
#include "core_client.hpp"
#include "logger.h"

namespace {

// Users with rank <= 3 (dev, owner, admin+, admin) are exempt from automatic
// moderation, mirroring the old Python bot.
constexpr int kPrivilegedMaxRank = 3;

double unixNow() {
    return std::chrono::duration<double>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

}  // namespace

#include OATPP_CODEGEN_BEGIN(DTO)

// Request body for POST /moderate.
class ModerateRequestDto : public oatpp::DTO {
    DTO_INIT(ModerateRequestDto, DTO)
    DTO_FIELD(Int64, chat_id);
    DTO_FIELD(Int64, user_id);
    DTO_FIELD(String, text);
    DTO_FIELD(Float64, timestamp);
};

class ModerateResponseDto : public oatpp::DTO {
    DTO_INIT(ModerateResponseDto, DTO)
    DTO_FIELD(String, action);              // "Allow" | "MuteTemporary" | "MutePermanent"
    DTO_FIELD(Int32, duration_seconds);     // only for MuteTemporary
    DTO_FIELD(Boolean, delete_message);
};

class ErrorDto : public oatpp::DTO {
    DTO_INIT(ErrorDto, DTO)
    DTO_FIELD(String, error);
};

#include OATPP_CODEGEN_END(DTO)

#include OATPP_CODEGEN_BEGIN(ApiController)

class AutoModController : public oatpp::web::server::api::ApiController {
public:
    AutoModController(const std::shared_ptr<oatpp::data::mapping::ObjectMapper>& objectMapper,
                      std::shared_ptr<AutoModerator> moderator,
                      std::shared_ptr<CoreClient> core)
        : oatpp::web::server::api::ApiController(objectMapper),
          objectMapper_(objectMapper),
          moderator_(std::move(moderator)),
          core_(std::move(core)) {}

    ENDPOINT("GET", "/", root) {
        return createResponse(Status::CODE_200, "pibot-auto-mod");
    }

    ENDPOINT("GET", "/health", health) {
        return createResponse(Status::CODE_200, "ok");
    }

    ENDPOINT("POST", "/moderate", moderate,
             REQUEST(std::shared_ptr<IncomingRequest>, request)) {
        // NOTE: readBodyToDto consumes the request body stream itself; the
        // body must not be read beforehand or the read blocks forever.
        ModerateRequestDto::Wrapper dto;
        try {
            dto = request->readBodyToDto<ModerateRequestDto::Wrapper>(objectMapper_);
        } catch (const std::exception& e) {
            const std::string detail = e.what();
            return badRequest(detail.empty() ? "Некорректный JSON"
                                             : "Некорректный JSON: " + detail);
        } catch (...) {
            return badRequest("Некорректный JSON");
        }

        // Fields are required. `timestamp` is optional and defaults to now so
        // callers that do not control the message clock can omit it.
        if (dto == nullptr || dto->chat_id == nullptr || dto->user_id == nullptr ||
            dto->text == nullptr) {
            return badRequest("Отсутствуют обязательные поля: chat_id, user_id, text");
        }

        // Privileged users (devs, owners, admins) are exempt from automatic
        // moderation. The rank is fetched from Core; if Core is unreachable the
        // message is still moderated (default member rank is 4).
        if (core_ != nullptr && core_->enabled()) {
            const int rank = core_->getChatRank(*dto->chat_id, *dto->user_id);
            if (rank >= 0 && rank <= kPrivilegedMaxRank) {
                Action action = moderator_->skip_privileged(
                    *dto->chat_id, *dto->user_id, dto->text->c_str());
                auto response = ModerateResponseDto::createShared();
                response->action = action_type_to_string(action.type);
                response->duration_seconds = action.duration_seconds;
                response->delete_message = action.delete_message;
                return createDtoResponse(Status::CODE_200, response);
            }
        }

        double timestamp =
            dto->timestamp != nullptr ? *dto->timestamp : unixNow();

        Action action = moderator_->process_message(
            *dto->chat_id, *dto->user_id, dto->text->c_str(), timestamp);

        auto response = ModerateResponseDto::createShared();
        response->action = action_type_to_string(action.type);
        response->duration_seconds = action.duration_seconds;
        response->delete_message = action.delete_message;
        return createDtoResponse(Status::CODE_200, response);
    }

private:
    std::shared_ptr<OutgoingResponse> badRequest(const std::string& message) {
        auto dto = ErrorDto::createShared();
        dto->error = message;
        return createDtoResponse(Status::CODE_400, dto);
    }

    std::shared_ptr<oatpp::data::mapping::ObjectMapper> objectMapper_;
    std::shared_ptr<AutoModerator> moderator_;
    std::shared_ptr<CoreClient> core_;
};

#include OATPP_CODEGEN_END(ApiController)

namespace {

std::atomic<bool> g_running{true};
std::shared_ptr<oatpp::network::Server> g_server;

void handleSignal(int) {
    g_running = false;
    if (g_server) {
        g_server->stop();
    }
}

std::string envOr(const char* name, const std::string& fallback = "") {
    const char* value = std::getenv(name);
    if (value == nullptr || *value == '\0') {
        return fallback;
    }
    return std::string(value);
}

int parseInt(const char* value, int fallback) {
    if (value == nullptr || *value == '\0') {
        return fallback;
    }
    char* end = nullptr;
    long parsed = std::strtol(value, &end, 10);
    if (end == value || *end != '\0') {
        return fallback;
    }
    return static_cast<int>(parsed);
}

spdlog::level::level_enum parseLogLevel(const std::string& level) {
    if (level == "debug") return spdlog::level::debug;
    if (level == "warn" || level == "warning") return spdlog::level::warn;
    if (level == "error") return spdlog::level::err;
    if (level == "critical") return spdlog::level::critical;
    return spdlog::level::info;
}

}  // namespace

int main() {
    std::signal(SIGINT, handleSignal);
    std::signal(SIGTERM, handleSignal);

    Logger::init(parseLogLevel(envOr("LOG_LEVEL", "info")));
    Logger::info("авто-модератор запускается");

    const int port = parseInt(std::getenv("AUTO_MOD_PORT"), 8083);
    auto core = std::make_shared<CoreClient>(
        envOr("CORE_HOST", "core"),
        static_cast<uint16_t>(parseInt(std::getenv("CORE_PORT"), 8080)),
        envOr("CORE_API_KEY"));
    Logger::info(core->enabled()
                     ? "интеграция с core включена: проверка рангов привилегированных пользователей"
                     : "интеграция с core отключена (CORE_API_KEY не задан), все сообщения проходят проверку");

    oatpp::base::Environment::init();
    auto objectMapper = std::make_shared<oatpp::parser::json::mapping::ObjectMapper>();
    auto router = oatpp::web::server::HttpRouter::createShared();
    auto moderator = std::make_shared<AutoModerator>();
    router->addController(
        std::make_shared<AutoModController>(objectMapper, moderator, core));
    auto handler = oatpp::web::server::HttpConnectionHandler::createShared(router);
    auto provider = oatpp::network::tcp::server::ConnectionProvider::createShared(
        {"0.0.0.0", static_cast<v_uint16>(port),
         oatpp::network::Address::IP_4});
    auto server = oatpp::network::Server::createShared(provider, handler);
    g_server = server;
    Logger::info("HTTP-сервер слушает порт " + std::to_string(port));

    server->run(std::function<bool()>([]() { return g_running.load(); }));
    Logger::info("HTTP-сервер остановлен");

    oatpp::base::Environment::destroy();
    Logger::info("авто-модератор остановлен");
    return 0;
}
