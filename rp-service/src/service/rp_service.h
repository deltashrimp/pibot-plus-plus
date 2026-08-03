#ifndef PIBOT_RP_SERVICE_H
#define PIBOT_RP_SERVICE_H

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include "model/rp_command.h"
#include "storage/redis_storage.h"

namespace rp {

// Business logic of the RP microservice: matches messages against stored
// commands, substitutes placeholders, and manages per-chat commands.
class RpService {
public:
    explicit RpService(std::shared_ptr<RedisStorage> storage);

    // Loads the predefined commands from a JSON file (trigger -> response map)
    // into memory. Returns false if the file is missing or cannot be parsed.
    bool loadPredefined(const std::string& path);

    // Adds any predefined commands that are missing in the chat's hash.
    // Existing (possibly customised) commands are never overwritten.
    void ensureDefaults(int64_t chatId);

    MatchResult match(int64_t chatId, int64_t userId, const std::string& text,
                      int64_t replyToUserId, const std::string& mention1,
                      const std::string& mention2);

    CommandResult addCommand(int64_t chatId, const std::string& trigger,
                             const std::string& response);
    CommandResult removeCommand(int64_t chatId, const std::string& trigger);
    CommandResult editCommand(int64_t chatId, const std::string& trigger,
                              const std::string& response);
    std::unordered_map<std::string, std::string> listCommands(int64_t chatId);

private:
    std::shared_ptr<RedisStorage> storage_;
    std::unordered_map<std::string, std::string> defaults_;
    std::mutex defaultsMutex_;
};

}  // namespace rp

#endif  // PIBOT_RP_SERVICE_H
