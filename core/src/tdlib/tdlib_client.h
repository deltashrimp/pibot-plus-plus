#ifndef PIBOT_TDLIB_CLIENT_H
#define PIBOT_TDLIB_CLIENT_H

#include <atomic>
#include <functional>
#include <map>
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
    // Same as sendTextPlain, but reports the created message's id via onSent
    // once TDLib confirms the send (0 on failure).
    void sendTextPlain(int64_t chatId, const std::string& text, int64_t replyToMessageId,
                       std::function<void(int64_t sentMessageId)> onSent);
    // Replaces the plain-text content of an already sent message.
    void editMessageText(int64_t chatId, int64_t messageId, const std::string& text);

    // Uploads and sends a local file as a document to the chat. The temporary
    // local copy is removed only after the upload actually completes (tracked
    // via updateMessageSendSucceeded / updateMessageSendFailed / updateDeleteMessages),
    // because TDLib keeps reading the file from disk while the upload runs.
    void sendDocument(int64_t chatId, const std::string& filePath,
                      int64_t replyToMessageId = 0);
    void resolveUsername(const std::string& username,
                         std::function<void(int64_t userId)> onResult,
                         std::function<void(const std::string&)> onError = nullptr);
    void getUserDisplayName(int64_t userId,
                            std::function<void(const std::string& displayName)> onResult,
                            std::function<void(const std::string&)> onError = nullptr);
    void getMessage(int64_t chatId, int64_t messageId,
                    std::function<void(int64_t senderUserId)> onResult,
                    std::function<void(const std::string&)> onError = nullptr);
    void getChatMember(int64_t chatId, int64_t userId,
                       std::function<void(bool isAdmin)> onResult,
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
    // Wraps content into sendMessage and sends it; a null callback gets the
    // default one that logs send failures.
    void sendMessageContent(int64_t chatId,
                            td::td_api::object_ptr<td::td_api::InputMessageContent> content,
                            int64_t replyToMessageId,
                            Callback callback = nullptr);
    void processUpdate(td::td_api::object_ptr<td::td_api::Object> update);
    void processAuthorizationState(td::td_api::object_ptr<td::td_api::Object> state);
    void deliverNewMessage(td::td_api::object_ptr<td::td_api::message> message);
    void handleDocumentSendFailure(
        td::td_api::object_ptr<td::td_api::updateMessageSendFailed> update);
    void handleDeletedMessages(td::td_api::object_ptr<td::td_api::updateDeleteMessages> update);
    // Delivers a sendTextPlain onSent callback once its send settles:
    // confirmedMessageId > 0 after updateMessageSendSucceeded, 0 on failure
    // or when the message was deleted meanwhile.
    void settlePendingSend(int64_t chatId, int64_t tempMessageId,
                           int64_t confirmedMessageId);
    void loop();

    // Removes the temp file registered for a sending document and its parent
    // directory once the send outcome is known. `messageId` is the temporary
    // (negative) message id used as the lookup key.
    void removePendingDocument(int64_t chatId, int64_t messageId);

    struct PendingDocument {
        int64_t chat_id = 0;
        std::string path;
    };

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
    std::mutex pendingDocsMutex_;
    // Temporary (negative) message id -> temp file that must be deleted when
    // the send of that message completes. Local message ids are only unique
    // per chat, hence the (chat_id, message_id) key.
    std::map<std::pair<int64_t, int64_t>, PendingDocument> pendingDocuments_;
    std::mutex pendingSendsMutex_;
    // Temporary (negative) message id -> caller waiting for the server
    // confirmed id of a plain-text send (sendTextPlain's onSent overload).
    std::map<std::pair<int64_t, int64_t>, std::function<void(int64_t)>> pendingTextSends_;
};

#endif
