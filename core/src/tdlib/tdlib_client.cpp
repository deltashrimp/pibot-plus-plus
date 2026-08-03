#include "tdlib/tdlib_client.h"

#include "logging/logger.h"

namespace {

template <class To, class From>
td::td_api::object_ptr<To> downcast(td::td_api::object_ptr<From>& obj) {
    return td::td_api::object_ptr<To>(static_cast<To*>(obj.release()));
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
            auto send = td::td_api::make_object<td::td_api::sendMessage>();
            send->chat_id_ = chatId;
            send->input_message_content_ = std::move(content);
            if (replyToMessageId > 0) {
                send->reply_to_message_id_ = replyToMessageId;
            }
            sendRequest(std::move(send),
                        [chatId](td::td_api::object_ptr<td::td_api::Object> sendResult) {
                            if (sendResult->get_id() == td::td_api::error::ID) {
                                auto err = downcast<td::td_api::error>(sendResult);
                                Logger::warn("failed to send message: " + err->message_, 0, chatId);
                            }
                        });
        });
}

void TdlibClient::sendTextPlain(int64_t chatId, const std::string& text,
                                int64_t replyToMessageId) {
    auto content = td::td_api::make_object<td::td_api::inputMessageText>();
    auto formatted = td::td_api::make_object<td::td_api::formattedText>();
    formatted->text_ = text;
    content->text_ = std::move(formatted);
    content->disable_web_page_preview_ = false;
    content->clear_draft_ = false;
    auto send = td::td_api::make_object<td::td_api::sendMessage>();
    send->chat_id_ = chatId;
    send->input_message_content_ = std::move(content);
    if (replyToMessageId > 0) {
        send->reply_to_message_id_ = replyToMessageId;
    }
    sendRequest(std::move(send),
                [chatId](td::td_api::object_ptr<td::td_api::Object> sendResult) {
                    if (sendResult->get_id() == td::td_api::error::ID) {
                        auto err = downcast<td::td_api::error>(sendResult);
                        Logger::warn("failed to send message: " + err->message_, 0, chatId);
                    }
                });
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

void TdlibClient::processUpdate(td::td_api::object_ptr<td::td_api::Object> update) {
    switch (update->get_id()) {
        case td::td_api::updateAuthorizationState::ID: {
            auto updateAuth = downcast<td::td_api::updateAuthorizationState>(update);
            processAuthorizationState(std::move(updateAuth->authorization_state_));
            break;
        }
        case td::td_api::updateNewMessage::ID: {
            auto updateMessage = downcast<td::td_api::updateNewMessage>(update);
            if (messageHandler_) {
                messageHandler_(std::move(updateMessage->message_));
            }
            break;
        }
        default:
            break;
    }
}

void TdlibClient::processAuthorizationState(td::td_api::object_ptr<td::td_api::Object> state) {
    switch (state->get_id()) {
        case td::td_api::authorizationStateWaitTdlibParameters::ID: {
            Logger::info("tdlib auth: waiting for parameters");
            auto params = td::td_api::make_object<td::td_api::tdlibParameters>();
            params->use_test_dc_ = false;
            params->api_id_ = apiId_;
            params->api_hash_ = apiHash_;
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
            sendRequestImpl(td::td_api::make_object<td::td_api::setTdlibParameters>(std::move(params)),
                            [](td::td_api::object_ptr<td::td_api::Object> result) {
                                if (result->get_id() == td::td_api::error::ID) {
                                    auto err = downcast<td::td_api::error>(result);
                                    Logger::error("tdlib: setTdlibParameters failed: " + err->message_);
                                }
                            });
            break;
        }
        case td::td_api::authorizationStateWaitPhoneNumber::ID: {
            Logger::info("tdlib auth: waiting for phone number, sending bot token");
            sendRequestImpl(td::td_api::make_object<td::td_api::checkAuthenticationBotToken>(token_),
                            [](td::td_api::object_ptr<td::td_api::Object> result) {
                                if (result->get_id() == td::td_api::error::ID) {
                                    auto err = downcast<td::td_api::error>(result);
                                    Logger::error("tdlib: bot token rejected: " + err->message_);
                                }
                            });
            break;
        }
        case td::td_api::authorizationStateWaitEncryptionKey::ID: {
            sendRequestImpl(td::td_api::make_object<td::td_api::setDatabaseEncryptionKey>(std::string()),
                            [](td::td_api::object_ptr<td::td_api::Object> result) {
                                if (result->get_id() == td::td_api::error::ID) {
                                    auto err = downcast<td::td_api::error>(result);
                                    Logger::error("tdlib: setDatabaseEncryptionKey failed: " + err->message_);
                                }
                            });
            break;
        }
        case td::td_api::authorizationStateReady::ID: {
            authorized_.store(true);
            Logger::info("tdlib authorized");
            break;
        }
        case td::td_api::authorizationStateClosed::ID: {
            Logger::warn("tdlib authorization closed");
            break;
        }
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
