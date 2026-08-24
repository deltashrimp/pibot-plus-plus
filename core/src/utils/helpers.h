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
// Removes zalgo (combining marks), zero-width/bidi and other invisible or
// text-breaking characters, and control bytes (newlines are kept). Malformed
// UTF-8 bytes are dropped. Everything else passes through unchanged.
std::string sanitizeForAi(const std::string& text);
// Clips text to at most maxBytes bytes without splitting a UTF-8 code point.
std::string truncateUtf8(const std::string& text, size_t maxBytes);
spdlog::level::level_enum parseLogLevel(const std::string& level);
int parseInt(const char* value, int fallback);

}  // namespace helpers

#endif
