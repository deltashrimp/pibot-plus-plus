#include "commands/moderation_commands.h"

#include <atomic>
#include <cctype>
#include <charconv>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <stdexcept>
#include <system_error>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include "database/db_manager.h"
#include "logging/logger.h"
#include "tdlib/tdlib_client.h"
#include "utils/helpers.h"

namespace {

// At most one command per user per second.
constexpr int64_t kCommandCooldownSeconds = 1;

// Placeholder text sent while the AI request runs; its content is replaced
// with the answer (or error) once the request completes.
constexpr const char* kAiThinkingMessage = "🟡 ИИ думает";

// Telegram hard limit for one message, used to clip AI answers before edit.
constexpr size_t kMaxTelegramMessageBytes = 4096;

// Number of recent chat messages passed to the AI as context.
constexpr size_t kAiContextMessages = 10;

// One context line of the /ai prompt.
struct AiContextEntry {
    std::string authorName;
    std::string text;
};

// Builds the /ai prompt: recent chat messages as quoted context followed by
// the asker's question, e.g.
//   Context: "`Ann says: Hello`, `Bob says: Hi`". Bob asks you "what's up?".
// Without context the plain question is sent as before.
std::string buildAiPrompt(const std::string& askerName, const std::string& question,
                          const std::vector<AiContextEntry>& entries) {
    if (entries.empty()) {
        return question;
    }
    std::string prompt = "Context: \"";
    for (size_t i = 0; i < entries.size(); ++i) {
        if (i != 0) {
            prompt += ", ";
        }
        prompt += "`" + entries[i].authorName + " says: " + entries[i].text + "`";
    }
    prompt += "\". ";
    prompt += askerName + " asks you \"" + question + "\".";
    return prompt;
}

// Parses the whole token as an integer; nullopt when the token is not a
// plain number. Used for user ids, where a failed parse is a normal outcome
// (the token is then treated as text), so no exceptions are involved.
std::optional<int64_t> parseInt64(const std::string& token) {
    int64_t value = 0;
    const char* end = token.data() + token.size();
    const auto [ptr, ec] = std::from_chars(token.data(), end, value);
    if (ec != std::errc() || ptr != end) {
        return std::nullopt;
    }
    return value;
}

template <class To, class From>
td::td_api::object_ptr<To> downcast(td::td_api::object_ptr<From>& obj) {
    return td::td_api::object_ptr<To>(static_cast<To*>(obj.release()));
}

bool writeFile(const std::string& path, const std::vector<char>& bytes) {
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        return false;
    }
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    return out.good();
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

// Dev-only (rank 0) commands listed by /devc; keep in sync with
// commandTable() and the requireRank(..., 0, ...) call sites.
const std::vector<std::pair<const char*, const char*>>& devCommandList() {
    static const std::vector<std::pair<const char*, const char*>> commands = {
        {"devc", "список dev-команд"},
        {"globalban <цель>", "добавить пользователя в глобальный бан"},
        {"globalunban <цель>", "убрать пользователя из глобального бана"},
    };
    return commands;
}

// First command argument, empty when there is none.
std::string firstArg(const CommandContext& context) {
    return !context.args.empty() ? context.args[0] : "";
}

// Sender user id of the message, 0 when the sender is not a user.
int64_t senderUserId(td::td_api::message& message) {
    if (!message.sender_id_ ||
        message.sender_id_->get_id() != td::td_api::messageSenderUser::ID) {
        return 0;
    }
    auto sender = downcast<td::td_api::messageSenderUser>(message.sender_id_);
    return sender->user_id_;
}

// Plain-text payload of the message; false when it is not a text message.
bool plainText(td::td_api::message& message, std::string& out) {
    if (!message.content_ || message.content_->get_id() != td::td_api::messageText::ID) {
        return false;
    }
    auto textContent = downcast<td::td_api::messageText>(message.content_);
    if (!textContent->text_) {
        return false;
    }
    out = textContent->text_->text_;
    return true;
}

// True when splitCommand output looks like "/command ..." input.
bool looksLikeCommand(const std::vector<std::string>& parts) {
    return !parts.empty() && !parts[0].empty() && parts[0][0] == '/';
}

// "/command@botname" -> "command".
std::string commandNameOf(const std::string& token) {
    std::string command = token.substr(1);
    size_t atPos = command.find('@');
    if (atPos != std::string::npos) {
        command.resize(atPos);
    }
    return command;
}

// Everything after leading whitespace and the command token itself.
std::string rawArgsAfterCommand(const std::string& text, const std::string& commandToken) {
    size_t start = 0;
    while (start < text.size() && std::isspace(static_cast<unsigned char>(text[start]))) {
        ++start;
    }
    size_t i = start + commandToken.size();
    while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i]))) {
        ++i;
    }
    return text.substr(i);
}

CommandContext buildContext(const td::td_api::message& message, int64_t chatId,
                            int64_t senderId, const std::string& command,
                            const std::vector<std::string>& parts, const std::string& text) {
    CommandContext context;
    context.chat_id = chatId;
    context.sender_id = senderId;
    context.message_id = message.id_;
    context.reply_chat_id = message.reply_in_chat_id_;
    context.reply_message_id = message.reply_to_message_id_;
    context.command = command;
    context.args.assign(parts.begin() + 1, parts.end());
    context.raw_args = rawArgsAfterCommand(text, parts[0]);
    return context;
}

// Parses the <2|3|4> argument of /rank. Returns 0 when the token is not a
// plain integer; other invalid values pass through and are rejected below.
int parseRankToken(const std::string& token) {
    try {
        size_t pos = 0;
        int rank = std::stoi(token, &pos);
        if (pos != token.size()) {
            return 0;
        }
        return rank;
    } catch (const std::exception&) {
        return 0;
    }
}

// Response payload of /rpadd and /rpedit: raw_args minus the trigger word and
// following whitespace.
std::string responseAfterTrigger(const CommandContext& context) {
    size_t i = context.args[0].size();
    while (i < context.raw_args.size() &&
           std::isspace(static_cast<unsigned char>(context.raw_args[i]))) {
        ++i;
    }
    return context.raw_args.substr(i);
}

// Display name of a TDLib user for the /ranks listing.
std::string formatUserName(const td::td_api::user& user) {
    if (!user.username_.empty()) {
        return "@" + user.username_;
    }
    std::string name = user.first_name_;
    if (!user.last_name_.empty()) {
        name += " " + user.last_name_;
    }
    return name;
}

// Stores the display name for userId in names, consuming the getUser result.
// Falls back to the raw id when the user object is unavailable.
void recordUserName(std::map<int64_t, std::string>& names, int64_t userId,
                    td::td_api::object_ptr<td::td_api::Object> result) {
    if (result->get_id() == td::td_api::user::ID) {
        names[userId] = formatUserName(*downcast<td::td_api::user>(result));
        return;
    }
    names[userId] = std::to_string(userId);
}

// Appends one "- <name>: <rank label> (<rank>)" line per entry.
void appendRankLines(std::string& text, const std::vector<RankEntry>& entries,
                     const std::map<int64_t, std::string>& names) {
    for (const auto& item : entries) {
        auto it = names.find(item.user_id);
        std::string name = it != names.end()
                               ? it->second
                               : std::to_string(item.user_id);
        text += "- " + name + ": " + rankLabel(item.rank) +
                " (" + std::to_string(item.rank) + ")\n";
    }
}

// What the first word after an RP trigger points at.
enum class RpTargetKind { None, Mention, UserId };

struct RpTarget {
    RpTargetKind kind = RpTargetKind::None;
    std::string username;  // Mention: username without the leading '@'
    int64_t userId = 0;    // UserId
};

RpTarget classifyRpTarget(const std::string& targetToken) {
    RpTarget target;
    if (targetToken.empty()) {
        return target;
    }
    if (targetToken[0] == '@') {
        target.kind = RpTargetKind::Mention;
        target.username = targetToken.substr(1);
        return target;
    }
    if (auto id = parseInt64(targetToken)) {
        target.kind = RpTargetKind::UserId;
        target.userId = *id;
    }
    return target;
}

}  // namespace

ModerationCommands::ModerationCommands(std::shared_ptr<TdlibClient> tdlib,
                                       std::shared_ptr<DbManager> db,
                                       std::shared_ptr<rp::RpClient> rpClient,
                                       std::shared_ptr<tools::ToolsClient> toolsClient,
                                       std::shared_ptr<ai::AiClient> aiClient)
    : tdlib_(std::move(tdlib)),
      db_(std::move(db)),
      rpClient_(std::move(rpClient)),
      toolsClient_(std::move(toolsClient)),
      aiClient_(std::move(aiClient)) {}

const std::unordered_map<std::string, ModerationCommands::CommandAction>&
ModerationCommands::commandTable() {
    static const std::unordered_map<std::string, CommandAction> table = {
        {"start", &ModerationCommands::executeStart},
        {"mute", &ModerationCommands::executeMute},
        {"unmute", &ModerationCommands::executeUnmute},
        {"kick", &ModerationCommands::executeKick},
        {"ban", &ModerationCommands::executeBan},
        {"unban", &ModerationCommands::executeUnban},
        {"globalban", &ModerationCommands::executeGlobalBan},
        {"globalunban", &ModerationCommands::executeGlobalUnban},
        {"devc", &ModerationCommands::executeDevCommands},
        {"rank", &ModerationCommands::executeRank},
        {"ranks", &ModerationCommands::executeRanks},
        {"rpadd", &ModerationCommands::executeRpAdd},
        {"rpremove", &ModerationCommands::executeRpRemove},
        {"rpedit", &ModerationCommands::executeRpEdit},
        {"rplist", &ModerationCommands::executeRpList},
        {"gclone", &ModerationCommands::executeGClone},
        {"ai", &ModerationCommands::executeAi},
    };
    return table;
}

bool ModerationCommands::canHandle(const std::string& command) const {
    return commandTable().count(command) > 0;
}

void ModerationCommands::handle(const CommandContext& context) {
    const auto it = commandTable().find(context.command);
    if (it != commandTable().end()) {
        (this->*it->second)(context);
    }
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

void ModerationCommands::handleMessage(td::td_api::object_ptr<td::td_api::message> message) {
    if (!message) {
        return;
    }

    const int64_t chatId = message->chat_id_;
    const int64_t senderId = senderUserId(*message);
    if (senderId == 0) {
        return;
    }
    if (db_->isGloballyBanned(senderId)) {
        Logger::info("ignoring message from globally banned user", senderId, chatId);
        return;
    }

    std::string text;
    if (!plainText(*message, text)) {
        return;
    }

    auto parts = helpers::splitCommand(text);
    if (!looksLikeCommand(parts)) {
        rememberChatMessage(chatId, senderId, helpers::sanitizeForAi(text));
        matchRp(chatId, senderId, text, message->reply_in_chat_id_,
                message->reply_to_message_id_, message->id_);
        return;
    }

    std::string command = commandNameOf(parts[0]);
    if (!canHandle(command)) {
        return;
    }
    if (!allowCommand(senderId)) {
        Logger::info("command dropped: rate limit exceeded", senderId, chatId, command);
        return;
    }

    Logger::info("command received", senderId, chatId, command);
    handle(buildContext(*message, chatId, senderId, command, parts, text));
}

ModerationCommands::ErrorReplier ModerationCommands::replier(const CommandContext& context) {
    return [this, context](const std::string& err) { reply(context, err); };
}

void ModerationCommands::resolveTarget(const std::string& targetToken,
                                       const CommandContext& context,
                                       std::function<void(int64_t userId)> onResolved,
                                       std::function<void(const std::string&)> onError) {
    if (!targetToken.empty()) {
        if (targetToken[0] == '@') {
            std::string username = targetToken.substr(1);
            tdlib_->resolveUsername(username, onResolved, onError);
            return;
        }
        auto id = parseInt64(targetToken);
        if (id) {
            onResolved(*id);
            return;
        }
        onError("Неверная цель: " + targetToken);
        return;
    }

    if (context.reply_message_id > 0) {
        int64_t replyChat = context.reply_chat_id != 0 ? context.reply_chat_id : context.chat_id;
        tdlib_->getMessage(replyChat, context.reply_message_id, onResolved, onError);
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

void ModerationCommands::moderateTarget(const CommandContext& context, int requiredRank,
                                        std::string targetToken,
                                        const std::string& cannotModerateMessage,
                                        TargetAction action) {
    requireRank(context, requiredRank,
                [this, context, targetToken, action, cannotModerateMessage] {
                    resolveTarget(targetToken, context,
                                  [this, context, action, cannotModerateMessage](
                                      int64_t targetId) {
                                      if (!canModerateTarget(context, targetId)) {
                                          reply(context, cannotModerateMessage);
                                          return;
                                      }
                                      action(targetId);
                                  },
                                  replier(context));
                },
                replier(context));
}

void ModerationCommands::banWithReply(const CommandContext& context, int64_t targetId,
                                      int32_t bannedUntilDate, bool revokeMessages,
                                      const std::string& successMessage,
                                      const std::string& failurePrefix) {
    tdlib_->banChatMember(
        context.chat_id, targetId, bannedUntilDate, revokeMessages,
        [this, context, successMessage, failurePrefix](
            td::td_api::object_ptr<td::td_api::Object> result) {
            if (result->get_id() == td::td_api::error::ID) {
                auto err = downcast<td::td_api::error>(result);
                reply(context, failurePrefix + " (" + err->message_ + ").");
                return;
            }
            reply(context, successMessage);
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

    moderateTarget(
        context, 3, targetIndex < context.args.size() ? context.args[targetIndex] : "",
        "Вы не можете замутить этого пользователя.",
        [this, context, until](int64_t targetId) {
            auto permissions = td::td_api::make_object<td::td_api::chatPermissions>(
                false, false, false, false, false, false, false, false);
            auto status = td::td_api::make_object<td::td_api::chatMemberStatusRestricted>(
                true, static_cast<int32_t>(until), std::move(permissions));
            setMemberStatus(context, targetId, std::move(status),
                            "Пользователь замучен.", "Не удалось замутить пользователя.");
        });
}

void ModerationCommands::executeUnmute(const CommandContext& context) {
    moderateTarget(
        context, 3, firstArg(context), "Вы не можете размутить этого пользователя.",
        [this, context](int64_t targetId) {
            db_->removeMute(context.chat_id, targetId);
            auto status = td::td_api::make_object<td::td_api::chatMemberStatusMember>();
            setMemberStatus(context, targetId, std::move(status),
                            "Пользователь размучен.", "Не удалось размутить пользователя.");
        });
}

void ModerationCommands::executeKick(const CommandContext& context) {
    moderateTarget(
        context, 2, firstArg(context), "Вы не можете кикнуть этого пользователя.",
        [this, context](int64_t targetId) {
            banWithReply(context, targetId, static_cast<int32_t>(helpers::unixNow()), false,
                         "Пользователь кикнут.", "Не удалось кикнуть пользователя");
        });
}

void ModerationCommands::executeBan(const CommandContext& context) {
    moderateTarget(
        context, 1, firstArg(context), "Вы не можете забанить этого пользователя.",
        [this, context](int64_t targetId) {
            banWithReply(context, targetId, 0, true,
                         "Пользователь забанен.", "Не удалось забанить пользователя");
        });
}

void ModerationCommands::executeUnban(const CommandContext& context) {
    moderateTarget(
        context, 1, firstArg(context), "Вы не можете разбанить этого пользователя.",
        [this, context](int64_t targetId) {
            auto status = td::td_api::make_object<td::td_api::chatMemberStatusMember>();
            setMemberStatus(context, targetId, std::move(status),
                            "Пользователь разбанен.", "Не удалось разбанить пользователя.");
        });
}

void ModerationCommands::executeGlobalBan(const CommandContext& context) {
    requireRank(context, 0,
                [this, context, targetToken = firstArg(context)] {
                    resolveTarget(targetToken, context,
                                  [this, context](int64_t targetId) {
                                      db_->addGlobalBan(targetId);
                                      reply(context, "Пользователь добавлен в глобальный бан.");
                                      Logger::info("global ban added", context.sender_id,
                                                   context.chat_id, context.command);
                                  },
                                  replier(context));
                },
                replier(context));
}

void ModerationCommands::executeGlobalUnban(const CommandContext& context) {
    requireRank(context, 0,
                [this, context, targetToken = firstArg(context)] {
                    resolveTarget(targetToken, context,
                                  [this, context](int64_t targetId) {
                                      db_->removeGlobalBan(targetId);
                                      reply(context, "Пользователь удалён из глобального бана.");
                                      Logger::info("global ban removed", context.sender_id,
                                                   context.chat_id, context.command);
                                  },
                                  replier(context));
                },
                replier(context));
}

void ModerationCommands::executeDevCommands(const CommandContext& context) {
    requireRank(
        context, 0,
        [this, context] {
            std::string text = "Dev-команды:\n";
            for (const auto& [command, description] : devCommandList()) {
                text += std::string("/") + command + " - " + description + "\n";
            }
            tdlib_->sendTextPlain(context.chat_id, text, context.message_id);
        },
        replier(context));
}

void ModerationCommands::applyRankChange(const CommandContext& context, int newRank,
                                         int64_t targetId) {
    if (!canModerateTarget(context, targetId)) {
        reply(context, "Вы не можете изменить ранг этого пользователя.");
        return;
    }
    tdlib_->getChatMember(
        context.chat_id, targetId,
        [this, context, newRank, targetId](bool isAdmin) {
            // Ranks 2/3 map to Telegram admins, rank 4 to regular members.
            const bool wantsAdmin = newRank == 2 || newRank == 3;
            if (wantsAdmin != isAdmin) {
                reply(context, wantsAdmin ? "Ранг 2/3 можно выдать только "
                                          "администратору Telegram. Сначала "
                                          "выдайте пользователю права "
                                          "администратора вручную."
                                          : "Цель всё ещё администратор Telegram. "
                                            "Сначала снимите её права администратора "
                                            "вручную, затем повторите /rank 4.");
                return;
            }
            db_->setChatRank(context.chat_id, targetId, newRank);
            Logger::info("rank set", context.sender_id, context.chat_id,
                         std::to_string(newRank) + " user=" + std::to_string(targetId));
            reply(context, "Ранг установлен.");
        },
        [this, context](const std::string& err) {
            reply(context,
                  "Не удалось проверить статус администратора "
                  "в Telegram (" +
                      err + ").");
        });
}

void ModerationCommands::executeRank(const CommandContext& context) {
    if (context.args.empty()) {
        reply(context, "Использование: /rank <2|3|4> <@username | id пользователя | ответ на сообщение>");
        return;
    }

    int newRank = parseRankToken(context.args[0]);
    if (newRank == 1) {
        reply(context, "Ранг 1 (владелец) не может быть выдан.");
        return;
    }
    if (newRank != 2 && newRank != 3 && newRank != 4) {
        reply(context, "Неверный ранг. Используйте 2, 3 или 4.");
        return;
    }

    moderateTarget(
        context, newRank == 2 ? 1 : 2, context.args.size() > 1 ? context.args[1] : "",
        "Вы не можете изменить ранг этого пользователя.",
        [this, context, newRank](int64_t targetId) {
            applyRankChange(context, newRank, targetId);
        });
}

void ModerationCommands::sendChatRanksReport(const CommandContext& context) {
    auto entries = db_->getChatRanks(context.chat_id);
    if (entries.empty()) {
        reply(context, "В этом чате нет пользователей с рангами 1-3.");
        return;
    }

    auto sharedEntries = std::make_shared<std::vector<RankEntry>>(std::move(entries));
    auto names = std::make_shared<std::map<int64_t, std::string>>();
    auto pending = std::make_shared<std::atomic<int>>(static_cast<int>(sharedEntries->size()));

    for (const auto& entry : *sharedEntries) {
        auto req = td::td_api::make_object<td::td_api::getUser>();
        req->user_id_ = entry.user_id;
        tdlib_->sendRequest(
            std::move(req),
            [this, context, entry, sharedEntries, names, pending](
                td::td_api::object_ptr<td::td_api::Object> result) {
                recordUserName(*names, entry.user_id, std::move(result));
                if (--(*pending) != 0) {
                    return;
                }
                std::string text = "Ранги в этом чате:\n";
                appendRankLines(text, *sharedEntries, *names);
                tdlib_->sendTextPlain(context.chat_id, text, context.message_id);
            });
    }
}

void ModerationCommands::executeRanks(const CommandContext& context) {
    requireRank(context, 4,
                [this, context] { sendChatRanksReport(context); },
                replier(context));
}

void ModerationCommands::matchRp(int64_t chatId, int64_t senderId, const std::string& text,
                                 int64_t replyChatId, int64_t replyMessageId, int64_t messageId) {
    if (!rpClient_ || !rpClient_->enabled() || text.empty()) {
        return;
    }

    auto parts = helpers::splitCommand(text);
    RpTarget target = classifyRpTarget(parts.size() > 1 ? parts[1] : "");

    // "обнять @username": resolve the mention and use it as the action target.
    if (target.kind == RpTargetKind::Mention) {
        tdlib_->resolveUsername(
            target.username,
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
    if (target.kind == RpTargetKind::UserId) {
        sendRpMatch(chatId, senderId, text, target.userId, messageId);
        return;
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

void ModerationCommands::reportRpResult(const CommandContext& context, bool ok,
                                        const std::string& message, const char* okLog,
                                        const char* failLog, const char* tag) {
    reply(context, message);
    Logger::info(ok ? okLog : failLog, context.sender_id, context.chat_id, tag);
}

void ModerationCommands::executeRpAdd(const CommandContext& context) {
    requireRank(
        context, 1,
        [this, context] {
            if (context.args.size() < 2 || context.raw_args.empty()) {
                reply(context, "Использование: /rpadd <триггер> <ответ>");
                return;
            }
            auto result = rpClient_->addCommand(context.chat_id, context.args[0],
                                                responseAfterTrigger(context));
            reportRpResult(context, result.ok, result.message,
                           "rp command added", "rp add failed", "rpadd");
        },
        replier(context));
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
            reportRpResult(context, result.ok, result.message,
                           "rp command removed", "rp remove failed", "rpremove");
        },
        replier(context));
}

void ModerationCommands::executeRpEdit(const CommandContext& context) {
    requireRank(
        context, 1,
        [this, context] {
            if (context.args.size() < 2 || context.raw_args.empty()) {
                reply(context, "Использование: /rpedit <триггер> <новый ответ>");
                return;
            }
            auto result = rpClient_->editCommand(context.chat_id, context.args[0],
                                                 responseAfterTrigger(context));
            reportRpResult(context, result.ok, result.message,
                           "rp command edited", "rp edit failed", "rpedit");
        },
        replier(context));
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
        replier(context));
}

void ModerationCommands::executeGClone(const CommandContext& context) {
    requireRank(context, 2,
                [this, context] {
                    if (context.args.empty()) {
                        reply(context, "Использование: /gclone <URL репозитория>");
                        return;
                    }
                    if (!toolsClient_ || !toolsClient_->enabled()) {
                        reply(context, "Git-клонер не настроен.");
                        return;
                    }

                    const std::string url = context.args[0];
                    reply(context, "Клонирую " + url + "...");

                    // The clone is a slow blocking operation; run it off the
                    // TDLib event loop so the bot keeps responding to other
                    // messages meanwhile.
                    std::thread([this, context, url] {
                        const auto result = toolsClient_->clone(url);
                        if (!result.ok) {
                            reply(context, result.error.empty()
                                               ? "Не удалось клонировать репозиторий."
                                               : result.error);
                            Logger::warn("gclone failed", context.sender_id,
                                         context.chat_id, result.error);
                            return;
                        }

                        // Unique temp directory so concurrent clones of the
                        // same repo never clobber each other's file; the file
                        // basename becomes the document name in the chat.
                        const std::string dir =
                            "/tmp/gclone_" + std::to_string(helpers::unixNow()) + "_" +
                            std::to_string(tempFileCounter_.fetch_add(1));
                        std::error_code ec;
                        if (!std::filesystem::create_directories(dir, ec)) {
                            reply(context, "Не удалось сохранить архив.");
                            return;
                        }
                        const std::string path = dir + "/" + result.archive_name;
                        if (!writeFile(path, result.archive_bytes)) {
                            reply(context, "Не удалось сохранить архив.");
                            std::filesystem::remove_all(dir, ec);
                            return;
                        }

                        Logger::info("gclone archive ready", context.sender_id,
                                     context.chat_id, result.archive_name);
                        tdlib_->sendDocument(context.chat_id, path, context.message_id);
                    }).detach();
                },
                replier(context));
}

void ModerationCommands::executeAi(const CommandContext& context) {
    requireRank(context, 4,
                [this, context] {
                    const std::string question = helpers::sanitizeForAi(context.raw_args);
                    if (question.empty()) {
                        reply(context, "Использование: /ai <сообщение>");
                        return;
                    }
                    if (!aiClient_ || !aiClient_->enabled()) {
                        reply(context, "AI-сервис не настроен.");
                        return;
                    }

                    // Immediate feedback: the placeholder's content is
                    // replaced with the answer (or error) below.
                    tdlib_->sendTextPlain(
                        context.chat_id, kAiThinkingMessage, context.message_id,
                        [this, context, question](int64_t placeholderMessageId) {
                            runAiWithHistory(context, question, placeholderMessageId);
                        });
                },
                replier(context));
}

void ModerationCommands::rememberChatMessage(int64_t chatId, int64_t senderId,
                                             std::string text) {
    std::lock_guard<std::mutex> lock(recentMutex_);
    auto& messages = recentMessages_[chatId];
    messages.push_back(RecentMessage{senderId, std::move(text)});
    while (messages.size() > kAiContextMessages) {
        messages.pop_front();
    }
}

std::vector<ModerationCommands::RecentMessage> ModerationCommands::recentChatMessages(
    int64_t chatId) {
    std::lock_guard<std::mutex> lock(recentMutex_);
    const auto it = recentMessages_.find(chatId);
    if (it == recentMessages_.end()) {
        return {};
    }
    return {it->second.begin(), it->second.end()};
}

void ModerationCommands::runAiWithHistory(const CommandContext& context,
                                          const std::string& question,
                                          int64_t placeholderMessageId) {
    auto history =
        std::make_shared<const std::vector<RecentMessage>>(recentChatMessages(context.chat_id));

    std::set<int64_t> userIds{context.sender_id};
    for (const auto& message : *history) {
        userIds.insert(message.senderId);
    }

    // Resolve every author's display name before building the prompt.
    auto names = std::make_shared<std::map<int64_t, std::string>>();
    auto pending = std::make_shared<std::atomic<int>>(static_cast<int>(userIds.size()));
    auto onNameResolved = [this, context, question, placeholderMessageId, history, names,
                           pending](int64_t userId, const std::string& displayName) {
        if (userId != 0 && !displayName.empty()) {
            (*names)[userId] = displayName;
        }
        if (--(*pending) != 0) {
            return;
        }
        finishAiRequest(context, question, placeholderMessageId, history, names);
    };

    for (int64_t userId : userIds) {
        tdlib_->getUserDisplayName(
            userId,
            [userId, onNameResolved](const std::string& displayName) {
                onNameResolved(userId, displayName);
            },
            [onNameResolved](const std::string&) { onNameResolved(0, ""); });
    }
}

void ModerationCommands::finishAiRequest(
    const CommandContext& context, const std::string& question,
    int64_t placeholderMessageId,
    std::shared_ptr<const std::vector<RecentMessage>> history,
    std::shared_ptr<const std::map<int64_t, std::string>> names) {
    std::vector<AiContextEntry> entries;
    entries.reserve(history->size());
    for (const auto& message : *history) {
        const auto author = names->find(message.senderId);
        const std::string authorName =
            author != names->end() ? author->second : "user " + std::to_string(message.senderId);
        entries.push_back({helpers::sanitizeForAi(authorName), message.text});
    }

    const auto asker = names->find(context.sender_id);
    const std::string askerName =
        helpers::sanitizeForAi(asker != names->end()
                                   ? asker->second
                                   : "user " + std::to_string(context.sender_id));

    // The AI call is a slow blocking operation; run it off the TDLib event
    // loop so the bot keeps responding to other messages meanwhile.
    std::thread([this, context, question, askerName, placeholderMessageId, entries] {
        const auto result = aiClient_->ask(buildAiPrompt(askerName, question, entries));
        if (!result.ok) {
            Logger::warn("ai ask failed", context.sender_id, context.chat_id, result.error);
            const std::string errorText = result.error.empty() ? "неизвестная ошибка" : result.error;
            replaceAiPlaceholder(context, placeholderMessageId, "ИИ умер: " + errorText);
            return;
        }
        Logger::info("ai response ready", context.sender_id, context.chat_id, "");
        replaceAiPlaceholder(context, placeholderMessageId, result.response);
    }).detach();
}

void ModerationCommands::replaceAiPlaceholder(const CommandContext& context,
                                              int64_t placeholderMessageId,
                                              const std::string& text) {
    if (placeholderMessageId <= 0) {
        // The placeholder itself could not be sent; answer with a new reply.
        tdlib_->sendTextPlain(context.chat_id, text, context.message_id);
        return;
    }
    tdlib_->editMessageText(
        context.chat_id, placeholderMessageId,
        helpers::truncateUtf8(text, kMaxTelegramMessageBytes),
        [this, context, text](bool ok) {
            if (!ok) {
                // The placeholder vanished (deleted or id never confirmed);
                // the answer must still reach the chat.
                Logger::warn("placeholder edit failed, replying fresh",
                             context.sender_id, context.chat_id, "");
                tdlib_->sendTextPlain(context.chat_id, text, context.message_id);
            }
        });
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
        "/rplist - список RP-команд в чате\n"
        "/gclone <URL> - скачать репозиторий архивом (.zip)\n"
        "/ai <сообщение> - спросить у ИИ\n\n"
        "RP-команды срабатывают обычным сообщением, например: обнять @user\n"
        "Цель RP — @username, числовой id или ответ на сообщение.\n"
        "В ответах RP-команд можно использовать {mention}, {mention1} (автор) "
        "и {mention2} (цель действия).\n\n"
        "<цель> — @username, числовой id или ответ на сообщение.";
    tdlib_->sendTextPlain(context.chat_id, text, context.message_id);
}
