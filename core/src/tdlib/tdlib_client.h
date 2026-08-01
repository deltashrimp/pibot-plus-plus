#ifndef PIBOT_TDLIB_CLIENT_H
#define PIBOT_TDLIB_CLIENT_H

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include <td/telegram/Client.h>
#include <td/telegram/td_api.h>
#include <td/telegram/td_api.hpp>

class TdlibClient {
public:
    using Callback = std::function<void(td::td_api::object_ptr<td::td_api::Object>)>;
    using MessageHandler = std::function<void(td::td_api::object_ptr<td::td_api::message>)>;

    TdlibClient();
    ~TdlibClient();

    void init(const std::string& token, int32_t apiId, const std::string& apiHash);
    void run();
    void stop();

    void sendRequest(td::td_api::object_ptr<td::td_api::Function> function,
                     Callback callback = nullptr);
    void sendText(int64_t chatId, const std::string& text, int64_t replyToMessageId = 0);
    void sendTextPlain(int64_t chatId, const std::string& text, int64_t replyToMessageId = 0);
    void resolveUsername(const std::string& username,
                         std::function<void(int64_t userId)> onResult,
                         std::function<void(const std::string&)> onError = nullptr);
    void getMessage(int64_t chatId, int64_t messageId,
                    std::function<void(int64_t senderUserId)> onResult,
                    std::function<void(const std::string&)> onError = nullptr);
    void setChatMemberStatus(int64_t chatId, int64_t userId,
                             td::td_api::object_ptr<td::td_api::ChatMemberStatus> status,
                             Callback callback = nullptr);
    void banChatMember(int64_t chatId, int64_t userId, int32_t bannedUntilDate,
                       bool revokeMessages, Callback callback = nullptr);

    void setMessageHandler(MessageHandler handler);
    bool authorized() const { return authorized_.load(); }

private:
    void sendRequestImpl(td::td_api::object_ptr<td::td_api::Function> function,
                         Callback callback = nullptr);
    void processUpdate(td::td_api::object_ptr<td::td_api::Object> update);
    void processAuthorizationState(td::td_api::object_ptr<td::td_api::Object> state);
    void loop();

    std::unique_ptr<td::Client> client_;
    std::string token_;
    std::string apiHash_;
    int32_t apiId_ = 0;
    std::atomic<bool> authorized_{false};
    std::atomic<bool> running_{false};
    std::mutex mutex_;
    uint64_t nextRequestId_ = 1;
    std::unordered_map<uint64_t, Callback> callbacks_;
    MessageHandler messageHandler_;
};

#endif
