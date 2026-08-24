#include "tdlib/tdlib_client.h"

#include <cstdio>
#include <filesystem>
#include <system_error>

#include "logging/logger.h"

namespace {

template <class To, class From>
td::td_api::object_ptr<To> downcast(td::td_api::object_ptr<From>& obj) {
    return td::td_api::object_ptr<To>(static_cast<To*>(obj.release()));
}

// Chat id of a message object, 0 when the message is absent.
int64_t chat_id_of(const td::td_api::object_ptr<td::td_api::message>& message) {
    return message ? message->chat_id_ : 0;
}

// Plain-text message content shared by sendTextPlain / editMessageText.
td::td_api::object_ptr<td::td_api::InputMessageContent> makePlainTextContent(
    const std::string& text) {
    auto content = td::td_api::make_object<td::td_api::inputMessageText>();
    auto formatted = td::td_api::make_object<td::td_api::formattedText>();
    formatted->text_ = text;
    content->text_ = std::move(formatted);
    content->disable_web_page_preview_ = false;
    content->clear_draft_ = false;
    return content;
}

// TDLib parameters for the bot's local database, as expected by
// authorizationStateWaitTdlibParameters.
td::td_api::object_ptr<td::td_api::tdlibParameters> make_tdlib_parameters(
    int32_t apiId, const std::string& apiHash) {
    auto params = td::td_api::make_object<td::td_api::tdlibParameters>();
    params->use_test_dc_ = false;
    params->api_id_ = apiId;
    params->api_hash_ = apiHash;
    params->database_directory_ = "/tmp/tdlib";
    params->files_directory_ = "/tmp/tdlib";
    params->use_file_database_ = false;
    params->use_chat_info_database_ = true;
    params->use_message_database_ = false;
    params->use_secret_chats_ = false;
    params->system_language_code_ = "en";
    params->device_model_ = "PiBot Core";
    params->system_version_ = "linux";
    params->application_version_ = "1.0.0";
    params->enable_storage_optimizer_ = true;
    params->ignore_file_names_ = false;
    return params;
}

// Callback that logs "<what><TDLib error message>" when the request fails.
TdlibClient::Callback log_error_callback(std::string what) {
    return [what = std::move(what)](td::td_api::object_ptr<td::td_api::Object> result) {
        if (result->get_id() == td::td_api::error::ID) {
            auto err = downcast<td::td_api::error>(result);
            Logger::error(what + err->message_);
        }
    };
}

}  // namespace

TdlibClient::TdlibClient() : client_(std::make_unique<td::Client>()) {}

TdlibClient::~TdlibClient() {
    stop();
}

void TdlibClient::init(const std::string& token, int32_t apiId, const std::string& apiHash) {
    token_ = token;
    apiId_ = apiId;
    apiHash_ = apiHash;
    td::Client::execute({0, td::td_api::make_object<td::td_api::setLogVerbosityLevel>(1)});
    Logger::info("tdlib client initialized");
}

void TdlibClient::run() {
    running_ = true;
    loop();
}

void TdlibClient::stop() {
    running_ = false;
    std::lock_guard<std::mutex> lock(mutex_);
    if (client_) {
        client_->send(td::Client::Request{0, td::td_api::make_object<td::td_api::close>()});
    }
}

void TdlibClient::setMessageHandler(MessageHandler handler) {
    messageHandler_ = std::move(handler);
}

void TdlibClient::sendRequest(td::td_api::object_ptr<td::td_api::Function> function,
                              Callback callback) {
    sendRequestImpl(std::move(function), std::move(callback));
}

void TdlibClient::sendRequestImpl(td::td_api::object_ptr<td::td_api::Function> function,
                                  Callback callback) {
    uint64_t id;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        id = nextRequestId_++;
        if (callback) {
            callbacks_[id] = std::move(callback);
        }
        client_->send(td::Client::Request{id, std::move(function)});
    }
}

void TdlibClient::sendMessageContent(
    int64_t chatId, td::td_api::object_ptr<td::td_api::InputMessageContent> content,
    int64_t replyToMessageId, Callback callback) {
    auto send = td::td_api::make_object<td::td_api::sendMessage>();
    send->chat_id_ = chatId;
    send->input_message_content_ = std::move(content);
    if (replyToMessageId > 0) {
        send->reply_to_message_id_ = replyToMessageId;
    }
    if (!callback) {
        callback = [chatId](td::td_api::object_ptr<td::td_api::Object> sendResult) {
            if (sendResult->get_id() == td::td_api::error::ID) {
                auto err = downcast<td::td_api::error>(sendResult);
                Logger::warn("failed to send message: " + err->message_, 0, chatId);
            }
        };
    }
    sendRequest(std::move(send), std::move(callback));
}

void TdlibClient::sendText(int64_t chatId, const std::string& text, int64_t replyToMessageId) {
    auto parse = td::td_api::make_object<td::td_api::parseTextEntities>();
    parse->text_ = text;
    parse->parse_mode_ = td::td_api::make_object<td::td_api::textParseModeMarkdown>(0);
    sendRequest(
        std::move(parse),
        [this, chatId, replyToMessageId](td::td_api::object_ptr<td::td_api::Object> result) {
            if (result->get_id() == td::td_api::error::ID) {
                auto err = downcast<td::td_api::error>(result);
                Logger::warn("failed to parse text entities: " + err->message_, 0, chatId);
                return;
            }
            auto parsed = downcast<td::td_api::formattedText>(result);
            auto content = td::td_api::make_object<td::td_api::inputMessageText>();
            content->text_ = std::move(parsed);
            content->disable_web_page_preview_ = false;
            content->clear_draft_ = false;
            sendMessageContent(chatId, std::move(content), replyToMessageId);
        });
}

void TdlibClient::sendTextPlain(int64_t chatId, const std::string& text,
                                int64_t replyToMessageId) {
    sendTextPlain(chatId, text, replyToMessageId, nullptr);
}

void TdlibClient::sendTextPlain(int64_t chatId, const std::string& text,
                                int64_t replyToMessageId,
                                std::function<void(int64_t sentMessageId)> onSent) {
    sendMessageContent(
        chatId, makePlainTextContent(text), replyToMessageId,
        [this, chatId, onSent = std::move(onSent)](
            td::td_api::object_ptr<td::td_api::Object> result) {
            if (result->get_id() == td::td_api::error::ID) {
                auto err = downcast<td::td_api::error>(result);
                Logger::warn("failed to send message: " + err->message_, 0, chatId);
                if (onSent) {
                    onSent(0);
                }
                return;
            }
            const int64_t sentId = downcast<td::td_api::message>(result)->id_;
            if (!onSent) {
                return;
            }
            Logger::info("text send ack", 0, chatId, "id=" + std::to_string(sentId));
            // Deliver the id only after updateMessageSendSucceeded confirms
            // the send. Even when TDLib pre-assigns the final positive id
            // (supergroups), the message stays in messageSendingStatePending
            // for a moment, and editing it that early fails with
            // "Message can't be edited".
            std::lock_guard<std::mutex> lock(pendingSendsMutex_);
            pendingTextSends_[{chatId, sentId}] = std::move(onSent);
        });
}

void TdlibClient::editMessageText(int64_t chatId, int64_t messageId, const std::string& text,
                                  std::function<void(bool ok)> onDone) {
    auto req = td::td_api::make_object<td::td_api::editMessageText>();
    req->chat_id_ = chatId;
    req->message_id_ = messageId;
    req->input_message_content_ = makePlainTextContent(text);
    sendRequest(std::move(req),
                [chatId, messageId, onDone = std::move(onDone)](
                    td::td_api::object_ptr<td::td_api::Object> result) {
                    const bool ok = result->get_id() != td::td_api::error::ID;
                    if (!ok) {
                        auto err = downcast<td::td_api::error>(result);
                        Logger::warn("failed to edit message " + std::to_string(messageId) +
                                         ": " + err->message_,
                                     0, chatId);
                    }
                    if (onDone) {
                        onDone(ok);
                    }
                });
}

void TdlibClient::sendDocument(int64_t chatId, const std::string& filePath,
                               int64_t replyToMessageId) {
    auto file = td::td_api::make_object<td::td_api::inputFileLocal>();
    file->path_ = filePath;
    auto content = td::td_api::make_object<td::td_api::inputMessageDocument>();
    content->document_ = std::move(file);
    content->disable_content_type_detection_ = false;
    // TDLib returns the created message immediately, before the file upload
    // has finished (MessagesManager::send_message returns synchronously), so
    // the temp file must NOT be removed here. It is registered by its temporary
    // (negative) message id and deleted later, when the upload outcome arrives
    // as updateMessageSendSucceeded / updateMessageSendFailed / updateDeleteMessages.
    auto callback = [this, chatId, filePath](td::td_api::object_ptr<td::td_api::Object> sendResult) {
        if (sendResult->get_id() == td::td_api::error::ID) {
            auto err = downcast<td::td_api::error>(sendResult);
            Logger::warn("failed to send document: " + err->message_, 0, chatId);
            std::error_code ec;
            std::filesystem::remove(filePath, ec);
            std::filesystem::remove(std::filesystem::path(filePath).parent_path(), ec);
            return;
        }
        auto msg = downcast<td::td_api::message>(sendResult);
        std::lock_guard<std::mutex> lock(pendingDocsMutex_);
        pendingDocuments_[{chatId, msg->id_}] = PendingDocument{chatId, filePath};
    };
    sendMessageContent(chatId, std::move(content), replyToMessageId, std::move(callback));
}

void TdlibClient::resolveUsername(const std::string& username,
                                  std::function<void(int64_t userId)> onResult,
                                  std::function<void(const std::string&)> onError) {
    auto req = td::td_api::make_object<td::td_api::searchPublicChat>();
    req->username_ = username;
    sendRequest(std::move(req),
                [onResult = std::move(onResult), onError = std::move(onError)](
                    td::td_api::object_ptr<td::td_api::Object> result) {
                    if (result->get_id() == td::td_api::error::ID) {
                        auto err = downcast<td::td_api::error>(result);
                        if (onError) {
                            onError("Не найдено: " + err->message_);
                        }
                        return;
                    }
                    auto chat = downcast<td::td_api::chat>(result);
                    if (chat->type_ &&
                        chat->type_->get_id() == td::td_api::chatTypePrivate::ID) {
                        auto privateType = downcast<td::td_api::chatTypePrivate>(chat->type_);
                        if (onResult) {
                            onResult(privateType->user_id_);
                        }
                        return;
                    }
                    if (onError) {
                        onError("Этот username не является пользователем (личный чат).");
                    }
                });
}

void TdlibClient::getUserDisplayName(int64_t userId,
                                     std::function<void(const std::string&)> onResult,
                                     std::function<void(const std::string&)> onError) {
    auto req = td::td_api::make_object<td::td_api::getUser>();
    req->user_id_ = userId;
    sendRequest(
        std::move(req),
        [onResult = std::move(onResult), onError = std::move(onError), userId](
            td::td_api::object_ptr<td::td_api::Object> result) {
            if (result->get_id() == td::td_api::error::ID) {
                auto err = downcast<td::td_api::error>(result);
                if (onError) {
                    onError(err->message_);
                }
                return;
            }
            auto user = downcast<td::td_api::user>(result);
            std::string name;
            if (!user->first_name_.empty()) {
                name = user->first_name_.c_str();
                if (!user->last_name_.empty()) {
                    name += " ";
                    name += user->last_name_.c_str();
                }
            } else if (!user->username_.empty()) {
                name = "@";
                name += user->username_.c_str();
            } else {
                name = "user " + std::to_string(userId);
            }
            if (onResult) {
                onResult(name);
            }
        });
}

void TdlibClient::getMessage(int64_t chatId, int64_t messageId,
                             std::function<void(int64_t senderUserId)> onResult,
                             std::function<void(const std::string&)> onError) {
    auto req = td::td_api::make_object<td::td_api::getMessage>();
    req->chat_id_ = chatId;
    req->message_id_ = messageId;
    sendRequest(std::move(req),
                [onResult = std::move(onResult), onError = std::move(onError)](
                    td::td_api::object_ptr<td::td_api::Object> result) {
                    if (result->get_id() == td::td_api::error::ID) {
                        auto err = downcast<td::td_api::error>(result);
                        if (onError) {
                            onError(err->message_);
                        }
                        return;
                    }
                    auto msg = downcast<td::td_api::message>(result);
                    if (msg->sender_id_ &&
                        msg->sender_id_->get_id() == td::td_api::messageSenderUser::ID) {
                        auto sender = downcast<td::td_api::messageSenderUser>(msg->sender_id_);
                        if (onResult) {
                            onResult(sender->user_id_);
                        }
                    } else if (onError) {
                        onError("target is not a user");
                    }
                });
}

void TdlibClient::getChatMember(int64_t chatId, int64_t userId,
                                std::function<void(bool isAdmin)> onResult,
                                std::function<void(const std::string&)> onError) {
    auto req = td::td_api::make_object<td::td_api::getChatMember>();
    req->chat_id_ = chatId;
    req->member_id_ = td::td_api::make_object<td::td_api::messageSenderUser>(userId);
    sendRequest(std::move(req),
                [onResult = std::move(onResult), onError = std::move(onError)](
                    td::td_api::object_ptr<td::td_api::Object> result) {
                    if (result->get_id() == td::td_api::error::ID) {
                        auto err = downcast<td::td_api::error>(result);
                        if (onError) {
                            onError(err->message_);
                        }
                        return;
                    }
                    auto member = downcast<td::td_api::chatMember>(result);
                    bool isAdmin = false;
                    if (member->status_) {
                        const int statusId = member->status_->get_id();
                        isAdmin =
                            statusId == td::td_api::chatMemberStatusAdministrator::ID ||
                            statusId == td::td_api::chatMemberStatusCreator::ID;
                    }
                    if (onResult) {
                        onResult(isAdmin);
                    }
                });
}

void TdlibClient::setChatMemberStatus(int64_t chatId, int64_t userId,
                                      td::td_api::object_ptr<td::td_api::ChatMemberStatus> status,
                                      Callback callback) {
    auto req = td::td_api::make_object<td::td_api::setChatMemberStatus>();
    req->chat_id_ = chatId;
    req->member_id_ = td::td_api::make_object<td::td_api::messageSenderUser>(userId);
    req->status_ = std::move(status);
    sendRequest(std::move(req), std::move(callback));
}

void TdlibClient::banChatMember(int64_t chatId, int64_t userId, int32_t bannedUntilDate,
                                bool revokeMessages, Callback callback) {
    auto req = td::td_api::make_object<td::td_api::banChatMember>();
    req->chat_id_ = chatId;
    req->member_id_ = td::td_api::make_object<td::td_api::messageSenderUser>(userId);
    req->banned_until_date_ = bannedUntilDate;
    req->revoke_messages_ = revokeMessages;
    sendRequest(std::move(req), std::move(callback));
}

void TdlibClient::removePendingDocument(int64_t chatId, int64_t messageId) {
    PendingDocument pending;
    {
        std::lock_guard<std::mutex> lock(pendingDocsMutex_);
        auto it = pendingDocuments_.find({chatId, messageId});
        if (it == pendingDocuments_.end()) {
            return;
        }
        pending = std::move(it->second);
        pendingDocuments_.erase(it);
    }
    Logger::info("document sent", pending.chat_id, 0, pending.path);
    std::error_code ec;
    std::filesystem::remove(pending.path, ec);
    std::filesystem::remove(std::filesystem::path(pending.path).parent_path(), ec);
}

void TdlibClient::deliverNewMessage(td::td_api::object_ptr<td::td_api::message> message) {
    if (messageHandler_) {
        messageHandler_(std::move(message));
    }
}

void TdlibClient::settlePendingSend(int64_t chatId, int64_t tempMessageId,
                                    int64_t confirmedMessageId) {
    std::function<void(int64_t)> onSent;
    {
        std::lock_guard<std::mutex> lock(pendingSendsMutex_);
        auto it = pendingTextSends_.find({chatId, tempMessageId});
        if (it == pendingTextSends_.end()) {
            return;
        }
        onSent = std::move(it->second);
        pendingTextSends_.erase(it);
    }
    Logger::info("text send settled", 0, chatId,
                 "temp=" + std::to_string(tempMessageId) +
                     " final=" + std::to_string(confirmedMessageId));
    // Invoke outside the lock: callbacks may re-enter TdlibClient.
    onSent(confirmedMessageId);
}

void TdlibClient::handleDocumentSendFailure(
    td::td_api::object_ptr<td::td_api::updateMessageSendFailed> update) {
    const int64_t chatId = chat_id_of(update->message_);
    Logger::warn("document send failed: " + update->error_message_, 0, chatId);
    removePendingDocument(chatId, update->old_message_id_);
    settlePendingSend(chatId, update->old_message_id_, 0);
    if (update->message_) {
        removePendingDocument(chatId, update->message_->id_);
    }
}

void TdlibClient::handleDeletedMessages(
    td::td_api::object_ptr<td::td_api::updateDeleteMessages> update) {
    // A sending file message can be irrecoverably deleted instead of
    // producing updateMessageSendFailed; clean up the temp file if one
    // of the deleted messages is a pending document of ours. Tracked text
    // sends are settled as failed so their callers can fall back.
    for (const auto messageId : update->message_ids_) {
        removePendingDocument(update->chat_id_, messageId);
        settlePendingSend(update->chat_id_, messageId, 0);
    }
}

void TdlibClient::processUpdate(td::td_api::object_ptr<td::td_api::Object> update) {
    switch (update->get_id()) {
        case td::td_api::updateAuthorizationState::ID: {
            auto auth = downcast<td::td_api::updateAuthorizationState>(update);
            processAuthorizationState(std::move(auth->authorization_state_));
            break;
        }
        case td::td_api::updateNewMessage::ID: {
            auto message = downcast<td::td_api::updateNewMessage>(update);
            deliverNewMessage(std::move(message->message_));
            break;
        }
        case td::td_api::updateMessageSendSucceeded::ID: {
            auto ok = downcast<td::td_api::updateMessageSendSucceeded>(update);
            const int64_t chatId = chat_id_of(ok->message_);
            removePendingDocument(chatId, ok->old_message_id_);
            settlePendingSend(chatId, ok->old_message_id_,
                              ok->message_ ? ok->message_->id_ : 0);
            break;
        }
        case td::td_api::updateMessageSendFailed::ID:
            handleDocumentSendFailure(downcast<td::td_api::updateMessageSendFailed>(update));
            break;
        case td::td_api::updateDeleteMessages::ID:
            handleDeletedMessages(downcast<td::td_api::updateDeleteMessages>(update));
            break;
        default:
            break;
    }
}

void TdlibClient::processAuthorizationState(td::td_api::object_ptr<td::td_api::Object> state) {
    switch (state->get_id()) {
        case td::td_api::authorizationStateWaitTdlibParameters::ID: {
            Logger::info("tdlib auth: waiting for parameters");
            sendRequestImpl(td::td_api::make_object<td::td_api::setTdlibParameters>(
                                make_tdlib_parameters(apiId_, apiHash_)),
                            log_error_callback("tdlib: setTdlibParameters failed: "));
            break;
        }
        case td::td_api::authorizationStateWaitPhoneNumber::ID: {
            Logger::info("tdlib auth: waiting for phone number, sending bot token");
            sendRequestImpl(td::td_api::make_object<td::td_api::checkAuthenticationBotToken>(token_),
                            log_error_callback("tdlib: bot token rejected: "));
            break;
        }
        case td::td_api::authorizationStateWaitEncryptionKey::ID:
            sendRequestImpl(
                td::td_api::make_object<td::td_api::setDatabaseEncryptionKey>(std::string()),
                log_error_callback("tdlib: setDatabaseEncryptionKey failed: "));
            break;
        case td::td_api::authorizationStateReady::ID:
            authorized_.store(true);
            Logger::info("tdlib authorized");
            break;
        case td::td_api::authorizationStateClosed::ID:
            Logger::warn("tdlib authorization closed");
            break;
        default:
            break;
    }
}

void TdlibClient::loop() {
    while (running_.load()) {
        auto response = client_->receive(1.0);
        if (!response.object) {
            continue;
        }
        if (response.id != 0) {
            Callback callback;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                auto it = callbacks_.find(response.id);
                if (it != callbacks_.end()) {
                    callback = std::move(it->second);
                    callbacks_.erase(it);
                }
            }
            if (callback) {
                callback(std::move(response.object));
            }
        } else {
            processUpdate(std::move(response.object));
        }
    }
    Logger::warn("tdlib loop stopped");
}
