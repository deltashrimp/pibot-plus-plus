#include "commands/moderation_commands.h"

#include <atomic>
#include <cctype>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <unordered_map>

#include "database/db_manager.h"
#include "logging/logger.h"
#include "tdlib/tdlib_client.h"
#include "utils/helpers.h"

namespace {

// At most one command per user per second.
constexpr int64_t kCommandCooldownSeconds = 1;

template <class To, class From>
td::td_api::object_ptr<To> downcast(td::td_api::object_ptr<From>& obj) {
    return td::td_api::object_ptr<To>(static_cast<To*>(obj.release()));
}

std::string rankLabel(int rank) {
    switch (rank) {
        case 0:
            return "разработчик";
        case 1:
            return "владелец";
        case 2:
            return "админ+";
        case 3:
            return "админ";
        default:
            return "участник";
    }
}

}  // namespace

ModerationCommands::ModerationCommands(std::shared_ptr<TdlibClient> tdlib,
                                       std::shared_ptr<DbManager> db,
                                       std::shared_ptr<rp::RpClient> rpClient)
    : tdlib_(std::move(tdlib)),
      db_(std::move(db)),
      rpClient_(std::move(rpClient)) {}

bool ModerationCommands::canHandle(const std::string& command) const {
    return command == "start" || command == "mute" || command == "unmute" ||
           command == "kick" || command == "ban" || command == "unban" ||
           command == "globalban" || command == "globalunban" || command == "rank" ||
           command == "ranks" || command == "rpadd" || command == "rpremove" ||
           command == "rpedit" || command == "rplist";
}

bool ModerationCommands::allowCommand(int64_t senderId) {
    std::lock_guard<std::mutex> lock(rateLimitMutex_);
    const int64_t now = helpers::unixNow();
    auto it = lastCommandAt_.find(senderId);
    if (it != lastCommandAt_.end() &&
        now - it->second < kCommandCooldownSeconds) {
        return false;
    }
    lastCommandAt_[senderId] = now;
    return true;
}

void ModerationCommands::handle(const CommandContext& context) {
    if (context.command == "start") {
        executeStart(context);
    } else if (context.command == "mute") {
        executeMute(context);
    } else if (context.command == "unmute") {
        executeUnmute(context);
    } else if (context.command == "kick") {
        executeKick(context);
    } else if (context.command == "ban") {
        executeBan(context);
    } else if (context.command == "unban") {
        executeUnban(context);
    } else if (context.command == "globalban") {
        executeGlobalBan(context);
    } else if (context.command == "globalunban") {
        executeGlobalUnban(context);
    } else if (context.command == "rank") {
        executeRank(context);
    } else if (context.command == "ranks") {
        executeRanks(context);
    } else if (context.command == "rpadd") {
        executeRpAdd(context);
    } else if (context.command == "rpremove") {
        executeRpRemove(context);
    } else if (context.command == "rpedit") {
        executeRpEdit(context);
    } else if (context.command == "rplist") {
        executeRpList(context);
    }
}

void ModerationCommands::handleMessage(td::td_api::object_ptr<td::td_api::message> message) {
    if (!message) {
        return;
    }

    int64_t chatId = message->chat_id_;
    int64_t senderId = 0;
    if (message->sender_id_ && message->sender_id_->get_id() == td::td_api::messageSenderUser::ID) {
        auto sender = downcast<td::td_api::messageSenderUser>(message->sender_id_);
        senderId = sender->user_id_;
    }
    if (senderId == 0) {
        return;
    }

    if (db_->isGloballyBanned(senderId)) {
        Logger::info("ignoring message from globally banned user", senderId, chatId);
        return;
    }

    if (!message->content_ || message->content_->get_id() != td::td_api::messageText::ID) {
        return;
    }
    auto textContent = downcast<td::td_api::messageText>(message->content_);
    if (!textContent->text_) {
        return;
    }
    std::string text = textContent->text_->text_;

    auto parts = helpers::splitCommand(text);
    if (parts.empty() || parts[0].empty() || parts[0][0] != '/') {
        matchRp(chatId, senderId, text, message->reply_in_chat_id_,
                message->reply_to_message_id_, message->id_);
        return;
    }

    std::string command = parts[0].substr(1);
    size_t atPos = command.find('@');
    if (atPos != std::string::npos) {
        command = command.substr(0, atPos);
    }
    if (!canHandle(command)) {
        return;
    }

    if (!allowCommand(senderId)) {
        Logger::info("command dropped: rate limit exceeded", senderId, chatId, command);
        return;
    }

    CommandContext context;
    context.chat_id = chatId;
    context.sender_id = senderId;
    context.message_id = message->id_;
    context.reply_chat_id = message->reply_in_chat_id_;
    context.reply_message_id = message->reply_to_message_id_;
    context.command = command;
    for (size_t i = 1; i < parts.size(); ++i) {
        context.args.push_back(parts[i]);
    }
    size_t tokenStart = 0;
    while (tokenStart < text.size() && std::isspace(static_cast<unsigned char>(text[tokenStart]))) {
        ++tokenStart;
    }
    size_t i = tokenStart + parts[0].size();
    while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i]))) {
        ++i;
    }
    context.raw_args = text.substr(i);

    Logger::info("command received", senderId, chatId, command);
    handle(context);
}

void ModerationCommands::resolveTarget(const std::string& targetToken,
                                       const CommandContext& context,
                                       std::function<void(int64_t userId)> onResolved,
                                       std::function<void(const std::string&)> onError) {
    if (!targetToken.empty()) {
        if (targetToken[0] == '@') {
            std::string username = targetToken.substr(1);
            tdlib_->resolveUsername(
                username,
                [onResolved](int64_t userId) { onResolved(userId); },
                [onError](const std::string& err) { onError(err); });
            return;
        }
        try {
            size_t pos = 0;
            long long id = std::stoll(targetToken, &pos);
            if (pos == targetToken.size()) {
                onResolved(id);
                return;
            }
        } catch (const std::exception&) {
        }
        onError("Неверная цель: " + targetToken);
        return;
    }

    if (context.reply_message_id > 0) {
        int64_t replyChat = context.reply_chat_id != 0 ? context.reply_chat_id : context.chat_id;
        tdlib_->getMessage(replyChat, context.reply_message_id,
                           [onResolved](int64_t userId) { onResolved(userId); },
                           [onError](const std::string& err) { onError(err); });
        return;
    }

    onError("Цель не указана. Ответьте на сообщение или передайте @username / id пользователя.");
}

void ModerationCommands::requireRank(const CommandContext& context, int requiredRank,
                                     std::function<void()> onOk,
                                     std::function<void(const std::string&)> onError) {
    int rank = db_->getChatRank(context.chat_id, context.sender_id);
    if (rank > requiredRank) {
        onError("Недостаточно прав для этой команды.");
        return;
    }
    onOk();
}

bool ModerationCommands::canModerateTarget(const CommandContext& context, int64_t targetId) {
    int callerRank = db_->getChatRank(context.chat_id, context.sender_id);
    int targetRank = db_->getChatRank(context.chat_id, targetId);
    return targetRank > callerRank;
}

void ModerationCommands::reply(const CommandContext& context, const std::string& text) {
    tdlib_->sendText(context.chat_id, helpers::escapeMarkdown(text), context.message_id);
}

void ModerationCommands::setMemberStatus(const CommandContext& context, int64_t targetId,
                                         td::td_api::object_ptr<td::td_api::ChatMemberStatus> status,
                                         const std::string& successMessage,
                                         const std::string& failureMessage) {
    tdlib_->setChatMemberStatus(
        context.chat_id, targetId, std::move(status),
        [this, context, successMessage, failureMessage](
            td::td_api::object_ptr<td::td_api::Object> result) {
            if (result->get_id() == td::td_api::error::ID) {
                auto err = downcast<td::td_api::error>(result);
                reply(context, failureMessage + " (" + err->message_ + ")");
                Logger::warn(failureMessage + ": " + err->message_, context.sender_id,
                             context.chat_id, context.command);
                return;
            }
            reply(context, successMessage);
            Logger::info(successMessage, context.sender_id, context.chat_id, context.command);
        });
}

void ModerationCommands::executeMute(const CommandContext& context) {
    int64_t until = 0;
    size_t targetIndex = 0;
    if (!context.args.empty()) {
        auto duration = helpers::parseDuration(context.args[0]);
        if (duration) {
            until = helpers::unixNow() + static_cast<int64_t>(duration->count());
            targetIndex = 1;
        }
    }

    std::string targetToken = targetIndex < context.args.size() ? context.args[targetIndex] : "";

    requireRank(context, 3,
                [this, context, until, targetToken] {
                    resolveTarget(targetToken, context,
                                  [this, context, until](int64_t targetId) {
                                      if (!canModerateTarget(context, targetId)) {
                                          reply(context, "Вы не можете замутить этого пользователя.");
                                          return;
                                      }
                                      auto permissions = td::td_api::make_object<
                                          td::td_api::chatPermissions>(false, false, false, false,
                                                                       false, false, false, false);
                                      auto status = td::td_api::make_object<
                                          td::td_api::chatMemberStatusRestricted>(
                                          true, static_cast<int32_t>(until), std::move(permissions));
                                      setMemberStatus(context, targetId, std::move(status),
                                                      "Пользователь замучен.",
                                                      "Не удалось замутить пользователя.");
                                  },
                                  [this, context](const std::string& err) { reply(context, err); });
                },
                [this, context](const std::string& err) { reply(context, err); });
}

void ModerationCommands::executeUnmute(const CommandContext& context) {
    std::string targetToken = !context.args.empty() ? context.args[0] : "";

    requireRank(context, 3,
                [this, context, targetToken] {
                    resolveTarget(targetToken, context,
                                  [this, context](int64_t targetId) {
                                      if (!canModerateTarget(context, targetId)) {
                                          reply(context, "Вы не можете размутить этого пользователя.");
                                          return;
                                      }
                                      db_->removeMute(context.chat_id, targetId);
                                      auto status =
                                          td::td_api::make_object<td::td_api::chatMemberStatusMember>();
                                      setMemberStatus(context, targetId, std::move(status),
                                                      "Пользователь размучен.",
                                                      "Не удалось размутить пользователя.");
                                  },
                                  [this, context](const std::string& err) { reply(context, err); });
                },
                [this, context](const std::string& err) { reply(context, err); });
}

void ModerationCommands::executeKick(const CommandContext& context) {
    std::string targetToken = !context.args.empty() ? context.args[0] : "";

    requireRank(context, 2,
                [this, context, targetToken] {
                    resolveTarget(targetToken, context,
                                  [this, context](int64_t targetId) {
                                      if (!canModerateTarget(context, targetId)) {
                                          reply(context, "Вы не можете кикнуть этого пользователя.");
                                          return;
                                      }
                                      int32_t now = static_cast<int32_t>(helpers::unixNow());
                                      tdlib_->banChatMember(
                                          context.chat_id, targetId, now, false,
                                          [this, context](td::td_api::object_ptr<td::td_api::Object> result) {
                                              if (result->get_id() == td::td_api::error::ID) {
                                                  auto err = downcast<td::td_api::error>(result);
                                                  reply(context, "Не удалось кикнуть пользователя (" + err->message_ + ").");
                                                  return;
                                              }
                                              reply(context, "Пользователь кикнут.");
                                          });
                                  },
                                  [this, context](const std::string& err) { reply(context, err); });
                },
                [this, context](const std::string& err) { reply(context, err); });
}

void ModerationCommands::executeBan(const CommandContext& context) {
    std::string targetToken = !context.args.empty() ? context.args[0] : "";

    requireRank(context, 1,
                [this, context, targetToken] {
                    resolveTarget(targetToken, context,
                                  [this, context](int64_t targetId) {
                                      if (!canModerateTarget(context, targetId)) {
                                          reply(context, "Вы не можете забанить этого пользователя.");
                                          return;
                                      }
                                      tdlib_->banChatMember(
                                          context.chat_id, targetId, 0, true,
                                          [this, context](td::td_api::object_ptr<td::td_api::Object> result) {
                                              if (result->get_id() == td::td_api::error::ID) {
                                                  auto err = downcast<td::td_api::error>(result);
                                                  reply(context, "Не удалось забанить пользователя (" + err->message_ + ").");
                                                  return;
                                              }
                                              reply(context, "Пользователь забанен.");
                                          });
                                  },
                                  [this, context](const std::string& err) { reply(context, err); });
                },
                [this, context](const std::string& err) { reply(context, err); });
}

void ModerationCommands::executeUnban(const CommandContext& context) {
    std::string targetToken = !context.args.empty() ? context.args[0] : "";

    requireRank(context, 1,
                [this, context, targetToken] {
                    resolveTarget(targetToken, context,
                                  [this, context](int64_t targetId) {
                                      if (!canModerateTarget(context, targetId)) {
                                          reply(context, "Вы не можете разбанить этого пользователя.");
                                          return;
                                      }
                                      auto status =
                                          td::td_api::make_object<td::td_api::chatMemberStatusMember>();
                                      setMemberStatus(context, targetId, std::move(status),
                                                      "Пользователь разбанен.",
                                                      "Не удалось разбанить пользователя.");
                                  },
                                  [this, context](const std::string& err) { reply(context, err); });
                },
                [this, context](const std::string& err) { reply(context, err); });
}

void ModerationCommands::executeGlobalBan(const CommandContext& context) {
    std::string targetToken = !context.args.empty() ? context.args[0] : "";

    requireRank(context, 0,
                [this, context, targetToken] {
                    resolveTarget(targetToken, context,
                                  [this, context](int64_t targetId) {
                                      db_->addGlobalBan(targetId);
                                      reply(context, "Пользователь добавлен в глобальный бан.");
                                      Logger::info("global ban added", context.sender_id,
                                                   context.chat_id, context.command);
                                  },
                                  [this, context](const std::string& err) { reply(context, err); });
                },
                [this, context](const std::string& err) { reply(context, err); });
}

void ModerationCommands::executeGlobalUnban(const CommandContext& context) {
    std::string targetToken = !context.args.empty() ? context.args[0] : "";

    requireRank(context, 0,
                [this, context, targetToken] {
                    resolveTarget(targetToken, context,
                                  [this, context](int64_t targetId) {
                                      db_->removeGlobalBan(targetId);
                                      reply(context, "Пользователь удалён из глобального бана.");
                                      Logger::info("global ban removed", context.sender_id,
                                                   context.chat_id, context.command);
                                  },
                                  [this, context](const std::string& err) { reply(context, err); });
                },
                [this, context](const std::string& err) { reply(context, err); });
}

void ModerationCommands::executeRank(const CommandContext& context) {
    if (context.args.empty()) {
        reply(context, "Использование: /rank <2|3|4> <@username | id пользователя | ответ на сообщение>");
        return;
    }

    int newRank = 0;
    try {
        size_t pos = 0;
        newRank = std::stoi(context.args[0], &pos);
        if (pos != context.args[0].size()) {
            throw std::invalid_argument("trailing chars");
        }
    } catch (const std::exception&) {
        reply(context, "Неверный ранг. Используйте 2, 3 или 4.");
        return;
    }
    if (newRank == 1) {
        reply(context, "Ранг 1 (владелец) не может быть выдан.");
        return;
    }
    if (newRank != 2 && newRank != 3 && newRank != 4) {
        reply(context, "Неверный ранг. Используйте 2, 3 или 4.");
        return;
    }

    std::string targetToken = context.args.size() > 1 ? context.args[1] : "";

    int requiredRank = newRank == 2 ? 1 : 2;
    requireRank(context, requiredRank,
                [this, context, newRank, targetToken] {
                    resolveTarget(targetToken, context,
                                  [this, context, newRank](int64_t targetId) {
                                      if (!canModerateTarget(context, targetId)) {
                                          reply(context, "Вы не можете изменить ранг этого пользователя.");
                                          return;
                                      }
                                      tdlib_->getChatMember(
                                          context.chat_id, targetId,
                                          [this, context, newRank, targetId](bool isAdmin) {
                                              if (newRank == 2 || newRank == 3) {
                                                  if (!isAdmin) {
                                                      reply(context,
                                                            "Ранг 2/3 можно выдать только "
                                                            "администратору Telegram. Сначала "
                                                            "выдайте пользователю права "
                                                            "администратора вручную.");
                                                      return;
                                                  }
                                              } else if (isAdmin) {
                                                  reply(context,
                                                        "Цель всё ещё администратор Telegram. "
                                                        "Сначала снимите её права администратора "
                                                        "вручную, затем повторите /rank 4.");
                                                  return;
                                              }
                                              db_->setChatRank(context.chat_id, targetId, newRank);
                                              Logger::info("rank set", context.sender_id, context.chat_id,
                                                           std::to_string(newRank) + " user=" +
                                                               std::to_string(targetId));
                                              reply(context, "Ранг установлен.");
                                          },
                                          [this, context](const std::string& err) {
                                              reply(context,
                                                    "Не удалось проверить статус администратора "
                                                    "в Telegram (" +
                                                        err + ").");
                                          });
                                  },
                                  [this, context](const std::string& err) { reply(context, err); });
                },
                [this, context](const std::string& err) { reply(context, err); });
}

void ModerationCommands::executeRanks(const CommandContext& context) {
    requireRank(context, 3,
                [this, context] {
                    auto entries = db_->getChatRanks(context.chat_id);
                    if (entries.empty()) {
                        reply(context, "В этом чате нет пользователей с рангами 1-3.");
                        return;
                    }

                    auto sharedEntries =
                        std::make_shared<std::vector<RankEntry>>(std::move(entries));
                    auto results = std::make_shared<std::map<int64_t, std::string>>();
                    auto pending = std::make_shared<std::atomic<int>>(
                        static_cast<int>(sharedEntries->size()));

                    for (const auto& entry : *sharedEntries) {
                        auto req = td::td_api::make_object<td::td_api::getUser>();
                        req->user_id_ = entry.user_id;
                        tdlib_->sendRequest(
                            std::move(req),
                            [this, context, entry, sharedEntries, results, pending](
                                td::td_api::object_ptr<td::td_api::Object> result) {
                                if (result->get_id() == td::td_api::user::ID) {
                                    auto user = downcast<td::td_api::user>(result);
                                    std::string name;
                                    if (!user->username_.empty()) {
                                        name = "@" + user->username_;
                                    } else {
                                        name = user->first_name_;
                                        if (!user->last_name_.empty()) {
                                            name += " " + user->last_name_;
                                        }
                                    }
                                    (*results)[entry.user_id] = name;
                                } else {
                                    (*results)[entry.user_id] = std::to_string(entry.user_id);
                                }
                                if (--(*pending) == 0) {
                                    std::string text = "Ранги в этом чате:\n";
                                    for (const auto& item : *sharedEntries) {
                                        auto it = results->find(item.user_id);
                                        std::string name =
                                            it != results->end()
                                                ? it->second
                                                : std::to_string(item.user_id);
                                        text += "- " + name + ": " + rankLabel(item.rank) +
                                                " (" + std::to_string(item.rank) + ")\n";
                                    }
                                    tdlib_->sendTextPlain(context.chat_id, text,
                                                          context.message_id);
                                }
                            });
                    }
                },
                [this, context](const std::string& err) { reply(context, err); });
}

void ModerationCommands::matchRp(int64_t chatId, int64_t senderId, const std::string& text,
                                 int64_t replyChatId, int64_t replyMessageId, int64_t messageId) {
    if (!rpClient_ || !rpClient_->enabled()) {
        return;
    }
    if (text.empty()) {
        return;
    }

    auto parts = helpers::splitCommand(text);
    std::string targetToken = parts.size() > 1 ? parts[1] : "";

    // "обнять @username": resolve the mention and use it as the action target.
    if (!targetToken.empty() && targetToken[0] == '@') {
        std::string username = targetToken.substr(1);
        tdlib_->resolveUsername(
            username,
            [this, chatId, senderId, text, messageId](int64_t userId) {
                sendRpMatch(chatId, senderId, text, userId, messageId);
            },
            [this, chatId, senderId, text, replyChatId, replyMessageId, messageId](
                const std::string&) {
                resolveRpReplyTarget(chatId, senderId, text, replyChatId, replyMessageId,
                                     messageId);
            });
        return;
    }

    // "обнять 123456": numeric user id as the action target.
    if (!targetToken.empty()) {
        try {
            size_t pos = 0;
            long long id = std::stoll(targetToken, &pos);
            if (pos == targetToken.size()) {
                sendRpMatch(chatId, senderId, text, id, messageId);
                return;
            }
        } catch (const std::exception&) {
        }
    }

    resolveRpReplyTarget(chatId, senderId, text, replyChatId, replyMessageId, messageId);
}

void ModerationCommands::resolveRpReplyTarget(int64_t chatId, int64_t senderId,
                                              const std::string& text, int64_t replyChatId,
                                              int64_t replyMessageId, int64_t messageId) {
    if (replyMessageId > 0) {
        int64_t replyChat = replyChatId != 0 ? replyChatId : chatId;
        tdlib_->getMessage(
            replyChat, replyMessageId,
            [this, chatId, senderId, text, messageId](int64_t replyToUserId) {
                sendRpMatch(chatId, senderId, text, replyToUserId, messageId);
            },
            [this, chatId, senderId, text, messageId](const std::string&) {
                sendRpMatch(chatId, senderId, text, 0, messageId);
            });
        return;
    }
    sendRpMatch(chatId, senderId, text, 0, messageId);
}

void ModerationCommands::sendRpMatch(int64_t chatId, int64_t senderId, const std::string& text,
                                     int64_t replyToUserId, int64_t messageId) {
    auto resolveTargetAndDispatch = [this, chatId, senderId, text, replyToUserId, messageId](
                                        const std::string& mention1) {
        if (replyToUserId <= 0) {
            dispatchRpMatch(chatId, senderId, text, 0, messageId, mention1, "");
            return;
        }
        tdlib_->getUserDisplayName(
            replyToUserId,
            [this, chatId, senderId, text, replyToUserId, messageId, mention1](
                const std::string& targetName) {
                dispatchRpMatch(chatId, senderId, text, replyToUserId, messageId, mention1,
                                helpers::mentionUser(replyToUserId, targetName));
            },
            [this, chatId, senderId, text, replyToUserId, messageId, mention1](
                const std::string&) {
                dispatchRpMatch(chatId, senderId, text, replyToUserId, messageId, mention1,
                                helpers::mentionUser(replyToUserId));
            });
    };

    tdlib_->getUserDisplayName(
        senderId,
        [this, resolveTargetAndDispatch, senderId](const std::string& senderName) {
            resolveTargetAndDispatch(helpers::mentionUser(senderId, senderName));
        },
        [this, resolveTargetAndDispatch, senderId](const std::string&) {
            resolveTargetAndDispatch(helpers::mentionUser(senderId));
        });
}

void ModerationCommands::dispatchRpMatch(int64_t chatId, int64_t senderId,
                                         const std::string& text, int64_t replyToUserId,
                                         int64_t messageId, const std::string& mention1,
                                         const std::string& mention2) {
    auto result = rpClient_->match(chatId, senderId, text, replyToUserId, mention1, mention2);
    if (result.matched && !result.response.empty()) {
        tdlib_->sendText(chatId, result.response, messageId);
        Logger::info("rp matched", senderId, chatId, result.trigger);
    }
}

void ModerationCommands::executeRpAdd(const CommandContext& context) {
    requireRank(
        context, 1,
        [this, context] {
            if (context.args.size() < 2 || context.raw_args.empty()) {
                reply(context, "Использование: /rpadd <триггер> <ответ>");
                return;
            }
            size_t i = context.args[0].size();
            while (i < context.raw_args.size() &&
                   std::isspace(static_cast<unsigned char>(context.raw_args[i]))) {
                ++i;
            }
            std::string response = context.raw_args.substr(i);
            auto result = rpClient_->addCommand(context.chat_id, context.args[0], response);
            reply(context, result.message);
            Logger::info(result.ok ? "rp command added" : "rp add failed", context.sender_id,
                         context.chat_id, "rpadd");
        },
        [this, context](const std::string& err) { reply(context, err); });
}

void ModerationCommands::executeRpRemove(const CommandContext& context) {
    requireRank(
        context, 1,
        [this, context] {
            if (context.args.empty()) {
                reply(context, "Использование: /rpremove <триггер>");
                return;
            }
            auto result = rpClient_->removeCommand(context.chat_id, context.args[0]);
            reply(context, result.message);
            Logger::info(result.ok ? "rp command removed" : "rp remove failed", context.sender_id,
                         context.chat_id, "rpremove");
        },
        [this, context](const std::string& err) { reply(context, err); });
}

void ModerationCommands::executeRpEdit(const CommandContext& context) {
    requireRank(
        context, 1,
        [this, context] {
            if (context.args.size() < 2 || context.raw_args.empty()) {
                reply(context, "Использование: /rpedit <триггер> <новый ответ>");
                return;
            }
            size_t i = context.args[0].size();
            while (i < context.raw_args.size() &&
                   std::isspace(static_cast<unsigned char>(context.raw_args[i]))) {
                ++i;
            }
            std::string response = context.raw_args.substr(i);
            auto result = rpClient_->editCommand(context.chat_id, context.args[0], response);
            reply(context, result.message);
            Logger::info(result.ok ? "rp command edited" : "rp edit failed", context.sender_id,
                         context.chat_id, "rpedit");
        },
        [this, context](const std::string& err) { reply(context, err); });
}

void ModerationCommands::executeRpList(const CommandContext& context) {
    requireRank(
        context, 1,
        [this, context] {
            auto commands = rpClient_->listCommands(context.chat_id);
            if (commands.empty()) {
                reply(context, "В этом чате нет RP-команд.");
                return;
            }
            std::string text = "RP-команды в этом чате:\n";
            for (const auto& entry : commands) {
                std::string response = entry.second;
                if (response.size() > 50) {
                    response = response.substr(0, 50) + "...";
                }
                text += entry.first + " - " + response + "\n";
            }
            tdlib_->sendTextPlain(context.chat_id, text, context.message_id);
        },
        [this, context](const std::string& err) { reply(context, err); });
}

void ModerationCommands::executeStart(const CommandContext& context) {
    const std::string text =
        "Привет! Я модератор-бот PiBot.\n\n"
        "Команды:\n"
        "/mute [длительность] <цель> - ограничить пользователя\n"
        "/unmute <цель> - снять ограничения\n"
        "/kick <цель> - исключить пользователя\n"
        "/ban <цель> - заблокировать пользователя\n"
        "/unban <цель> - разблокировать пользователя\n"
        "/rank <2 / 3 / 4> <цель> - установить ранг (2/3 только для админов)\n"
        "/ranks - список рангов в чате\n"
        "/rpadd <триггер> <ответ> - добавить RP-команду\n"
        "/rpremove <триггер> - удалить RP-команду\n"
        "/rpedit <триггер> <ответ> - изменить RP-команду\n"
        "/rplist - список RP-команд в чате\n\n"
        "RP-команды срабатывают обычным сообщением, например: обнять @user\n"
        "Цель RP — @username, числовой id или ответ на сообщение.\n"
        "В ответах RP-команд можно использовать {mention}, {mention1} (автор) "
        "и {mention2} (цель действия).\n\n"
        "<цель> — @username, числовой id или ответ на сообщение.";
    tdlib_->sendTextPlain(context.chat_id, text, context.message_id);
}
