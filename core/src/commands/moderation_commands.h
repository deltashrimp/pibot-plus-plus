#ifndef PIBOT_MODERATION_COMMANDS_H
#define PIBOT_MODERATION_COMMANDS_H

#include <functional>
#include <memory>

#include <td/telegram/td_api.h>
#include <td/telegram/td_api.hpp>

#include "commands/command_handler.h"

class TdlibClient;
class DbManager;

class ModerationCommands : public CommandHandler {
public:
    ModerationCommands(std::shared_ptr<TdlibClient> tdlib, std::shared_ptr<DbManager> db);

    bool canHandle(const std::string& command) const override;
    void handle(const CommandContext& context) override;

    void handleMessage(td::td_api::object_ptr<td::td_api::message> message);

private:
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

    void resolveTarget(const std::string& targetToken, const CommandContext& context,
                       std::function<void(int64_t userId)> onResolved,
                       std::function<void(const std::string&)> onError);
    void requireRank(const CommandContext& context, int requiredRank,
                     std::function<void()> onOk,
                     std::function<void(const std::string&)> onError);
    bool canModerateTarget(const CommandContext& context, int64_t targetId);
    void reply(const CommandContext& context, const std::string& text);
    void applyTelegramRank(const CommandContext& context, int64_t targetId, int rank);
    void setMemberStatus(const CommandContext& context, int64_t targetId,
                         td::td_api::object_ptr<td::td_api::ChatMemberStatus> status,
                         const std::string& successMessage, const std::string& failureMessage);

    std::shared_ptr<TdlibClient> tdlib_;
    std::shared_ptr<DbManager> db_;
};

#endif
