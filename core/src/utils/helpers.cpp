#include "utils/helpers.h"

#include <cctype>
#include <cstdlib>

namespace helpers {

std::vector<std::string> splitCommand(const std::string& text) {
    std::vector<std::string> parts;
    size_t i = 0;
    while (i < text.size()) {
        while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i]))) {
            ++i;
        }
        size_t start = i;
        while (i < text.size() && !std::isspace(static_cast<unsigned char>(text[i]))) {
            ++i;
        }
        if (i > start) {
            parts.push_back(text.substr(start, i - start));
        }
    }
    return parts;
}

std::optional<std::chrono::seconds> parseDuration(const std::string& text) {
    if (text.empty()) {
        return std::nullopt;
    }
    size_t i = 0;
    long long value = 0;
    while (i < text.size() && std::isdigit(static_cast<unsigned char>(text[i]))) {
        value = value * 10 + (text[i] - '0');
        ++i;
    }
    if (i == 0) {
        return std::nullopt;
    }
    long long multiplier = 0;
    if (i == text.size()) {
        multiplier = 60;
    } else {
        switch (text[i]) {
            case 's': multiplier = 1; break;
            case 'm': multiplier = 60; break;
            case 'h': multiplier = 3600; break;
            case 'd': multiplier = 86400; break;
            case 'w': multiplier = 7 * 86400; break;
            case 'M': multiplier = 30 * 86400; break;
            case 'y': multiplier = 365 * 86400; break;
            default: return std::nullopt;
        }
        ++i;
        if (i != text.size()) {
            return std::nullopt;
        }
    }
    return std::chrono::seconds(value * multiplier);
}

int64_t unixNow() {
    return static_cast<int64_t>(std::chrono::duration_cast<std::chrono::seconds>(
                                    std::chrono::system_clock::now().time_since_epoch())
                                    .count());
}

std::string mentionUser(int64_t userId) {
    return "[user " + std::to_string(userId) + "](tg://user?id=" + std::to_string(userId) + ")";
}

std::string mentionUser(int64_t userId, const std::string& name) {
    std::string escaped;
    escaped.reserve(name.size());
    for (char c : name) {
        if (c == '\\' || c == '`' || c == '*' || c == '_' || c == '{' || c == '}' ||
            c == '[' || c == ']' || c == '(' || c == ')' || c == '#') {
            escaped += '\\';
        }
        escaped += c;
    }
    return "[" + escaped + "](tg://user?id=" + std::to_string(userId) + ")";
}

spdlog::level::level_enum parseLogLevel(const std::string& level) {
    if (level == "debug") return spdlog::level::debug;
    if (level == "warn" || level == "warning") return spdlog::level::warn;
    if (level == "error") return spdlog::level::err;
    if (level == "critical") return spdlog::level::critical;
    return spdlog::level::info;
}

int parseInt(const char* value, int fallback) {
    if (value == nullptr || *value == '\0') {
        return fallback;
    }
    char* end = nullptr;
    long parsed = std::strtol(value, &end, 10);
    if (end == value || *end != '\0') {
        return fallback;
    }
    return static_cast<int>(parsed);
}

}  // namespace helpers
