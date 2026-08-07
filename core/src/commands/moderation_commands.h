#ifndef PIBOT_MODERATION_COMMANDS_H
#define PIBOT_MODERATION_COMMANDS_H

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <unordered_map>

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

    void executeMute(const CommandContext& context);
    void executeUnmute(const CommandContext& context);
    void executeKick(const CommandContext& context);
    void executeBan(const CommandContext& context);
    void executeUnban(const CommandContext& context);
    void executeGlobalBan(const CommandContext& context);
    void executeGlobalUnban(const CommandContext& context);
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

    // Unique suffix for temporary /gclone files so concurrent clones of the
    // same repository never write to the same path.
    std::atomic<uint64_t> tempFileCounter_{0};
};

#endif
