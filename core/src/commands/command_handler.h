#ifndef PIBOT_COMMAND_HANDLER_H
#define PIBOT_COMMAND_HANDLER_H

#include <string>
#include <vector>

struct CommandContext {
    int64_t chat_id = 0;
    int64_t sender_id = 0;
    int64_t message_id = 0;
    int64_t reply_chat_id = 0;
    int64_t reply_message_id = 0;
    std::string command;
    std::vector<std::string> args;
    std::string raw_args;
};

class CommandHandler {
public:
    virtual ~CommandHandler() = default;

    virtual bool canHandle(const std::string& command) const = 0;
    virtual void handle(const CommandContext& context) = 0;
};

#endif
