#ifndef PIBOT_HELPERS_H
#define PIBOT_HELPERS_H

#include <chrono>
#include <optional>
#include <string>
#include <vector>

#include <spdlog/spdlog.h>

namespace helpers {

std::vector<std::string> splitCommand(const std::string& text);
std::optional<std::chrono::seconds> parseDuration(const std::string& text);
int64_t unixNow();
std::string escapeMarkdown(const std::string& text);
std::string mentionUser(int64_t userId);
std::string mentionUser(int64_t userId, const std::string& name);
spdlog::level::level_enum parseLogLevel(const std::string& level);
int parseInt(const char* value, int fallback);

}  // namespace helpers

#endif
