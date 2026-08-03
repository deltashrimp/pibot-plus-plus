#ifndef PIBOT_RP_REDIS_STORAGE_H
#define PIBOT_RP_REDIS_STORAGE_H

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>

#include <sw/redis++/redis++.h>

namespace rp {

// Persists RP commands in Redis hashes. Commands for a chat live under the key
// `rp:commands:{chat_id}` where the hash field is the trigger and the hash
// value is the response template.
//
// Thread-safe: `sw::redis::Redis` wraps a connection pool and serializes
// access internally. All methods degrade gracefully (empty result / false) on
// Redis errors instead of throwing.
class RedisStorage {
public:
    RedisStorage(std::string host, uint16_t port, size_t poolSize = 8);

    bool ping();

    std::optional<std::string> getCommand(int64_t chatId, const std::string& trigger);
    bool hasCommand(int64_t chatId, const std::string& trigger);

    // Returns true only when the field was created (trigger did not exist).
    bool addCommand(int64_t chatId, const std::string& trigger, const std::string& response);

    // Adds the field only if it does not exist yet (HSETNX). Used to seed
    // predefined commands without overwriting user customisations.
    bool addIfAbsent(int64_t chatId, const std::string& trigger, const std::string& response);

    // Overwrites the response for an existing (or new) trigger.
    bool updateCommand(int64_t chatId, const std::string& trigger, const std::string& response);

    // Returns true only when an existing field was removed.
    bool removeCommand(int64_t chatId, const std::string& trigger);

    std::unordered_map<std::string, std::string> listCommands(int64_t chatId);
    size_t countCommands(int64_t chatId);

private:
    std::string keyFor(int64_t chatId) const;

    std::string host_;
    uint16_t port_;
    sw::redis::Redis redis_;
};

}  // namespace rp

#endif  // PIBOT_RP_REDIS_STORAGE_H
