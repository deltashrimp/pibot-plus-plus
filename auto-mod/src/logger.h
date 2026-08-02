#ifndef PIBOT_LOGGER_H
#define PIBOT_LOGGER_H

#include <memory>
#include <string>

#include <spdlog/spdlog.h>

// Structured JSON logging to stdout, mirroring the Core service.
class Logger {
public:
    static void init(spdlog::level::level_enum level);
    static std::shared_ptr<spdlog::logger> get();

    static void info(const std::string& message);
    static void warn(const std::string& message);
    static void error(const std::string& message);
};

#endif
