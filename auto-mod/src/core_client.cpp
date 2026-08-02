#include "core_client.hpp"

#include <chrono>
#include <exception>
#include <string>

#include <oatpp/core/macro/codegen.hpp>
#include <oatpp/network/Address.hpp>
#include <oatpp/network/tcp/client/ConnectionProvider.hpp>
#include <oatpp/parser/json/mapping/ObjectMapper.hpp>
#include <oatpp/web/client/HttpRequestExecutor.hpp>
#include <oatpp/web/protocol/http/Http.hpp>

#include "logger.h"

namespace {

double unix_now() {
    return std::chrono::duration<double>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

#include OATPP_CODEGEN_BEGIN(DTO)

// Matches Core's RankDto (core/src/api/api_controller.h).
class CoreRankDto : public oatpp::DTO {
    DTO_INIT(CoreRankDto, DTO)
    DTO_FIELD(Int64, chat_id);
    DTO_FIELD(Int64, user_id);
    DTO_FIELD(Int32, rank);
};

#include OATPP_CODEGEN_END(DTO)

}  // namespace

CoreClient::CoreClient(std::string host, uint16_t port, std::string api_key)
    : host_(std::move(host)), port_(port), api_key_(std::move(api_key)) {}

int CoreClient::getChatRank(int64_t chat_id, int64_t user_id) {
    {
        std::lock_guard<std::mutex> lock(cache_mutex_);
        auto chat = cache_.find(chat_id);
        if (chat != cache_.end()) {
            auto entry = chat->second.find(user_id);
            if (entry != chat->second.end() &&
                unix_now() - entry->second.fetched_at < kCacheTtlSeconds) {
                return entry->second.rank;
            }
        }
    }

    int rank = -1;
    try {
        auto connectionProvider =
            oatpp::network::tcp::client::ConnectionProvider::createShared(
                {host_.c_str(), port_, oatpp::network::Address::IP_4});
        auto executor =
            oatpp::web::client::HttpRequestExecutor::createShared(
                connectionProvider);

        oatpp::web::protocol::http::Headers headers;
        headers.put("X-API-Key", oatpp::String(api_key_.c_str()));

        const std::string path = "/config/chat/" + std::to_string(chat_id) +
                                 "/rank/" + std::to_string(user_id);

        auto response = executor->execute("GET", path.c_str(), headers,
                                          nullptr, nullptr);
        if (response != nullptr && response->getStatusCode() == 200) {
            auto objectMapper =
                std::make_shared<oatpp::parser::json::mapping::ObjectMapper>();
            auto dto = response->readBodyToDto<oatpp::Object<CoreRankDto>>(
                objectMapper);
            if (dto != nullptr && dto->rank != nullptr) {
                rank = *dto->rank;
            }
        }
    } catch (const std::exception& e) {
        Logger::warn(std::string(
                         "не удалось получить ранг пользователя из core: ") +
                     e.what());
    }

    if (rank >= 0) {
        std::lock_guard<std::mutex> lock(cache_mutex_);
        cache_[chat_id][user_id] = {rank, unix_now()};
    }
    return rank;
}
