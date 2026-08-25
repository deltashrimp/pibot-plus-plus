#ifndef PIBOT_MODERATION_COMMANDS_H
#define PIBOT_MODERATION_COMMANDS_H

#include <atomic>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <td/telegram/td_api.h>
#include <td/telegram/td_api.hpp>

#include "ai/ai_client.h"
#include "commands/command_handler.h"
#include "rp/rp_client.h"
#include "tools/tools_client.h"

class TdlibClient;
class DbManager;

class ModerationCommands : public CommandHandler {
public:
    ModerationCommands(std::shared_ptr<TdlibClient> tdlib, std::shared_ptr<DbManager> db,
                       std::shared_ptr<rp::RpClient> rpClient,
                       std::shared_ptr<tools::ToolsClient> toolsClient,
                       std::shared_ptr<ai::AiClient> aiClient);

    bool canHandle(const std::string& command) const override;
    void handle(const CommandContext& context) override;

    void handleMessage(td::td_api::object_ptr<td::td_api::message> message);

private:
    // Returns true if the user may run a command now; enforces a cooldown of
    // one command per user per second.
    bool allowCommand(int64_t senderId);

    using CommandAction = void (ModerationCommands::*)(const CommandContext&);
    // Command -> handler dispatch table shared by canHandle() and handle().
    // A static member (not a file-local helper) so it can form pointers to
    // the private execute* handlers.
    static const std::unordered_map<std::string, CommandAction>& commandTable();

    using ErrorReplier = std::function<void(const std::string&)>;
    using TargetAction = std::function<void(int64_t targetId)>;

    // Standard error path for command callbacks: sends err to the chat.
    ErrorReplier replier(const CommandContext& context);

    // Shared pipeline of the single-target moderation commands:
    // requireRank -> resolveTarget -> canModerateTarget -> action. Replies
    // with cannotModerateMessage when the caller must not act on the target.
    void moderateTarget(const CommandContext& context, int requiredRank,
                        std::string targetToken,
                        const std::string& cannotModerateMessage, TargetAction action);

    // Bans the target and replies with the outcome (no logging, unlike
    // setMemberStatus).
    void banWithReply(const CommandContext& context, int64_t targetId,
                      int32_t bannedUntilDate, bool revokeMessages,
                      const std::string& successMessage,
                      const std::string& failurePrefix);

    // Fetches chat ranks and sends the formatted /ranks report once every
    // user lookup has completed.
    void sendChatRanksReport(const CommandContext& context);

    // Verifies the target's Telegram admin status against newRank, then
    // stores the rank change.
    void applyRankChange(const CommandContext& context, int newRank, int64_t targetId);

    // Replies with result.message and logs the outcome under tag.
    void reportRpResult(const CommandContext& context, bool ok, const std::string& message,
                        const char* okLog, const char* failLog, const char* tag);

    // A non-command chat message kept as /ai context.
    struct RecentMessage {
        int64_t senderId = 0;
        std::string text;
    };
    // Stores a sanitized chat message in the per-chat rolling history.
    void rememberChatMessage(int64_t chatId, int64_t senderId, std::string text);
    // Copy of the chat's recent messages, oldest first.
    std::vector<RecentMessage> recentChatMessages(int64_t chatId);

    // Resolves display names for the /ai context, then starts the request.
    void runAiWithHistory(const CommandContext& context, const std::string& question,
                          int64_t placeholderMessageId);
    // Builds the prompt and runs the blocking AI call on a worker thread,
    // then swaps the placeholder for the answer (or error).
    void finishAiRequest(const CommandContext& context, const std::string& question,
                         int64_t placeholderMessageId,
                         std::shared_ptr<const std::vector<RecentMessage>> history,
                         std::shared_ptr<const std::map<int64_t, std::string>> names);
    // Replaces the "ИИ думает" placeholder content with the final text.
    // Falls back to a plain reply when the placeholder was never sent.
    void replaceAiPlaceholder(const CommandContext& context, int64_t placeholderMessageId,
                              const std::string& text);

    void executeMute(const CommandContext& context);
    void executeUnmute(const CommandContext& context);
    void executeKick(const CommandContext& context);
    void executeBan(const CommandContext& context);
    void executeUnban(const CommandContext& context);
    void executeGlobalBan(const CommandContext& context);
    void executeGlobalUnban(const CommandContext& context);
    // Replies with the list of dev-only (rank 0) commands.
    void executeDevCommands(const CommandContext& context);
    void executeRank(const CommandContext& context);
    void executeRanks(const CommandContext& context);
    void executeStart(const CommandContext& context);
    void executeRpAdd(const CommandContext& context);
    void executeRpRemove(const CommandContext& context);
    void executeRpEdit(const CommandContext& context);
    void executeRpList(const CommandContext& context);
    void executeGClone(const CommandContext& context);
    void executeAi(const CommandContext& context);

    // Forwards a non-command message to the RP service and sends the matched
    // response. No-op when the RP service is not configured.
    void matchRp(int64_t chatId, int64_t senderId, const std::string& text,
                 int64_t replyChatId, int64_t replyMessageId, int64_t messageId);
    void sendRpMatch(int64_t chatId, int64_t senderId, const std::string& text,
                     int64_t replyToUserId, int64_t messageId);
    void dispatchRpMatch(int64_t chatId, int64_t senderId, const std::string& text,
                         int64_t replyToUserId, int64_t messageId,
                         const std::string& mention1, const std::string& mention2);
    void resolveRpReplyTarget(int64_t chatId, int64_t senderId, const std::string& text,
                              int64_t replyChatId, int64_t replyMessageId, int64_t messageId);

    void resolveTarget(const std::string& targetToken, const CommandContext& context,
                       std::function<void(int64_t userId)> onResolved,
                       std::function<void(const std::string&)> onError);
    void requireRank(const CommandContext& context, int requiredRank,
                     std::function<void()> onOk,
                     std::function<void(const std::string&)> onError);
    bool canModerateTarget(const CommandContext& context, int64_t targetId);
    void reply(const CommandContext& context, const std::string& text);
    void setMemberStatus(const CommandContext& context, int64_t targetId,
                         td::td_api::object_ptr<td::td_api::ChatMemberStatus> status,
                         const std::string& successMessage, const std::string& failureMessage);

    std::shared_ptr<TdlibClient> tdlib_;
    std::shared_ptr<DbManager> db_;
    std::shared_ptr<rp::RpClient> rpClient_;
    std::shared_ptr<tools::ToolsClient> toolsClient_;
    std::shared_ptr<ai::AiClient> aiClient_;

    std::mutex rateLimitMutex_;
    std::unordered_map<int64_t, int64_t> lastCommandAt_;

    // Per-chat rolling history of recent non-command messages, used as /ai
    // context (newest last).
    std::mutex recentMutex_;
    std::unordered_map<int64_t, std::deque<RecentMessage>> recentMessages_;

    // Unique suffix for temporary /gclone files so concurrent clones of the
    // same repository never write to the same path.
    std::atomic<uint64_t> tempFileCounter_{0};
};

#endif
