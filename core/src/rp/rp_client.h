#ifndef PIBOT_RP_CLIENT_H
#define PIBOT_RP_CLIENT_H

#include <cstdint>
#include <string>
#include <unordered_map>

namespace rp {

struct MatchResult {
    bool matched = false;
    std::string response;
    std::string trigger;
};

struct CommandResult {
    bool ok = false;
    std::string message;
};

// Minimal HTTP client for the RP service REST API (X-API-Key protected).
// Used by the moderation commands to manage RP commands and to match normal
// messages against stored triggers. All methods are safe to call from a single
// thread (the TDLib event loop); each call opens a short-lived connection.
class RpClient {
public:
    // `baseUrl` like "http://rp:8081".
    RpClient(std::string baseUrl, std::string apiKey);

    // Requests are only sent when both a URL and an API key are configured;
    // without an API key the RP service rejects everything with 401.
    bool enabled() const { return !baseUrl_.empty() && !apiKey_.empty(); }

    MatchResult match(int64_t chatId, int64_t userId, const std::string& text,
                      int64_t replyToUserId, const std::string& mention1 = "",
                      const std::string& mention2 = "");

    CommandResult addCommand(int64_t chatId, const std::string& trigger,
                             const std::string& response);
    CommandResult removeCommand(int64_t chatId, const std::string& trigger);
    CommandResult editCommand(int64_t chatId, const std::string& trigger,
                              const std::string& response);

    // Returns the chat's commands (trigger -> response). Empty on failure.
    std::unordered_map<std::string, std::string> listCommands(int64_t chatId);

private:
    CommandResult executeCommand(const std::string& action, int64_t chatId,
                                 const std::string& trigger, const std::string& response);

    std::string baseUrl_;
    std::string apiKey_;
};

}  // namespace rp

#endif  // PIBOT_RP_CLIENT_H
