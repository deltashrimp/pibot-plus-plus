#ifndef PIBOT_RP_COMMAND_H
#define PIBOT_RP_COMMAND_H

#include <string>

namespace rp {

// A single role-play command mapping: trigger word -> response template.
struct Command {
    std::string trigger;
    std::string response;
};

// Result of trying to match a message against stored RP commands.
struct MatchResult {
    bool matched = false;
    std::string response;
    std::string trigger;
};

// Result of a command management operation (add/remove/edit).
struct CommandResult {
    bool ok = false;
    std::string message;
};

}  // namespace rp

#endif  // PIBOT_RP_COMMAND_H
