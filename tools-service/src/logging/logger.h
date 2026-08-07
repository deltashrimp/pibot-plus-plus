#ifndef PIBOT_TOOLS_LOGGER_H
#define PIBOT_TOOLS_LOGGER_H

#include <cstdint>
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

    // Emit a structured JSON event:
    //   {"action":"...","detail":"..."}
    static void event(const std::string& action, const std::string& detail = "",
                      spdlog::level::level_enum level = spdlog::level::info);
};

#endif  // PIBOT_TOOLS_LOGGER_H
