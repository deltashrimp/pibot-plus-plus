#include "commands/moderation_commands.h"

#include <atomic>
#include <cctype>
#include <map>
#include <memory>
#include <stdexcept>

#include "database/db_manager.h"
#include "logging/logger.h"
#include "tdlib/tdlib_client.h"
#include "utils/helpers.h"

namespace {

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
                                       std::shared_ptr<DbManager> db)
    : tdlib_(std::move(tdlib)), db_(std::move(db)) {}

bool ModerationCommands::canHandle(const std::string& command) const {
    return command == "start" || command == "mute" || command == "unmute" ||
           command == "kick" || command == "ban" || command == "unban" ||
           command == "globalban" || command == "globalunban" || command == "rank" ||
           command == "ranks";
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
    tdlib_->sendText(context.chat_id, text, context.message_id);
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
                                      db_->setChatRank(context.chat_id, targetId, newRank);
                                      Logger::info("rank set", context.sender_id, context.chat_id,
                                                   std::to_string(newRank) + " user=" +
                                                       std::to_string(targetId));
                                      applyTelegramRank(context, targetId, newRank);
                                  },
                                  [this, context](const std::string& err) { reply(context, err); });
                },
                [this, context](const std::string& err) { reply(context, err); });
}

void ModerationCommands::applyTelegramRank(const CommandContext& context, int64_t targetId,
                                           int rank) {
    td::td_api::object_ptr<td::td_api::ChatMemberStatus> status;
    if (rank == 2 || rank == 3) {
        auto admin = td::td_api::make_object<td::td_api::chatMemberStatusAdministrator>();
        admin->custom_title_ = "";
        admin->can_be_edited_ = false;
        admin->can_manage_chat_ = true;
        admin->can_change_info_ = true;
        admin->can_post_messages_ = true;
        admin->can_edit_messages_ = true;
        admin->can_delete_messages_ = true;
        admin->can_invite_users_ = true;
        admin->can_restrict_members_ = true;
        admin->can_pin_messages_ = true;
        admin->can_promote_members_ = false;
        admin->can_manage_video_chats_ = false;
        admin->is_anonymous_ = false;
        status = std::move(admin);
    } else {
        status = td::td_api::make_object<td::td_api::chatMemberStatusMember>();
    }
    setMemberStatus(context, targetId, std::move(status),
                    rank == 4 ? "Ранг установлен. Права администратора сняты."
                              : "Ранг установлен. Выданы права администратора.",
                    "Ранг сохранён, но не удалось обновить статус администратора в Telegram.");
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

void ModerationCommands::executeStart(const CommandContext& context) {
    const std::string text =
        "Привет! Я модератор-бот PiBot.\n\n"
        "Команды:\n"
        "/mute [длительность] <цель> - ограничить пользователя\n"
        "/unmute <цель> - снять ограничения\n"
        "/kick <цель> - исключить пользователя\n"
        "/ban <цель> - заблокировать пользователя\n"
        "/unban <цель> - разблокировать пользователя\n"
        "/globalban <цель> - добавить в глобальный бан\n"
        "/globalunban <цель> - удалить из глобального бана\n"
        "/rank <2|3|4> <цель> - установить ранг\n"
        "/ranks - список рангов в чате\n\n"
        "<цель> — @username, числовой id или ответ на сообщение.";
    tdlib_->sendTextPlain(context.chat_id, text, context.message_id);
}
