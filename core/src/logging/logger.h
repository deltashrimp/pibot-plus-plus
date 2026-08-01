#ifndef PIBOT_LOGGER_H
#define PIBOT_LOGGER_H

#include <memory>
#include <string>

#include <spdlog/spdlog.h>

class Logger {
public:
    static void init(spdlog::level::level_enum level);
    static std::shared_ptr<spdlog::logger> get();

    static void info(const std::string& message);
    static void warn(const std::string& message);
    static void error(const std::string& message);

    static void info(const std::string& message, int64_t userId, int64_t chatId = -1,
                     const std::string& command = "");
    static void warn(const std::string& message, int64_t userId, int64_t chatId = -1,
                     const std::string& command = "");
    static void error(const std::string& message, int64_t userId, int64_t chatId = -1,
                      const std::string& command = "");
};

#endif
