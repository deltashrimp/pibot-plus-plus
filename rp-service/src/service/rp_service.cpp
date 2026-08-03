#include "service/rp_service.h"

#include <fstream>
#include <iterator>
#include <string>

#include <oatpp/core/data/mapping/type/UnorderedMap.hpp>
#include <oatpp/parser/json/mapping/ObjectMapper.hpp>

#include "logging/logger.h"
#include "service/command_matcher.h"
#include "service/variable_substitutor.h"

namespace {

constexpr size_t kMaxTriggerLength = 64;
constexpr size_t kMaxResponseLength = 2000;

bool isWhitespace(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

// Lowercases ASCII (A-Z) and Cyrillic (А-Я, Ё) in a UTF-8 string. Triggers are
// Russian words, and <cctype>'s std::tolower does not handle Cyrillic, so a
// minimal UTF-8-aware fold is needed. Other code points are passed through.
std::string toLower(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    size_t i = 0;
    while (i < s.size()) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if (c < 0x80) {
            out += static_cast<char>((c >= 'A' && c <= 'Z') ? c + 32 : c);
            ++i;
            continue;
        }
        if ((c & 0xE0) == 0xC0 && i + 1 < s.size()) {
            const uint32_t cp =
                ((static_cast<uint32_t>(c) & 0x1F) << 6) |
                (static_cast<uint32_t>(static_cast<unsigned char>(s[i + 1])) & 0x3F);
            uint32_t lc = cp;
            if (cp >= 0x400 && cp <= 0x40F) {
                lc = cp + 0x50;  // Ѐ-Џ -> ѐ-џ
            } else if (cp >= 0x410 && cp <= 0x42F) {
                lc = cp + 0x20;  // А-Я -> а-я
            } else if (cp == 0x401) {
                lc = 0x451;      // Ё -> ё
            }
            out += static_cast<char>(0xD0 | (lc >> 6));
            out += static_cast<char>(0x80 | (lc & 0x3F));
            i += 2;
            continue;
        }
        out += s[i];
        ++i;
    }
    return out;
}

std::string trim(const std::string& s) {
    size_t begin = 0;
    while (begin < s.size() && isWhitespace(s[begin])) {
        ++begin;
    }
    size_t end = s.size();
    while (end > begin && isWhitespace(s[end - 1])) {
        --end;
    }
    return s.substr(begin, end - begin);
}

// Triggers are matched case-insensitively: they are always stored (and looked
// up) in lowercase.
std::string normalizeTrigger(const std::string& trigger) {
    return toLower(trim(trigger));
}

bool isValidTrigger(const std::string& trigger) {
    if (trigger.empty() || trigger.size() > kMaxTriggerLength) {
        return false;
    }
    for (char c : trigger) {
        if (isWhitespace(c)) {
            return false;
        }
    }
    return true;
}

bool isValidResponse(const std::string& response) {
    return !response.empty() && response.size() <= kMaxResponseLength;
}

}  // namespace

namespace rp {

RpService::RpService(std::shared_ptr<RedisStorage> storage)
    : storage_(std::move(storage)) {}

bool RpService::loadPredefined(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        Logger::warn("predefined commands file not found: " + path);
        return false;
    }
    std::string content((std::istreambuf_iterator<char>(file)),
                        std::istreambuf_iterator<char>());
    if (content.empty()) {
        Logger::warn("predefined commands file is empty: " + path);
        return false;
    }

    auto objectMapper = std::make_shared<oatpp::parser::json::mapping::ObjectMapper>();
    try {
        using PhraseMap =
            oatpp::data::mapping::type::UnorderedMap<oatpp::String, oatpp::String>;
        auto map = objectMapper->readFromString<PhraseMap>(content.c_str());
        std::lock_guard<std::mutex> lock(defaultsMutex_);
        defaults_.clear();
        for (const auto& pair : *map) {
            defaults_[normalizeTrigger(pair.first->c_str())] = pair.second->c_str();
        }
    } catch (const std::exception& e) {
        Logger::warn("failed to parse predefined commands file: " +
                     std::string(e.what()));
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(defaultsMutex_);
        Logger::info("loaded " + std::to_string(defaults_.size()) +
                     " predefined commands");
    }
    return true;
}

void RpService::ensureDefaults(int64_t chatId) {
    std::unordered_map<std::string, std::string> defaults;
    {
        std::lock_guard<std::mutex> lock(defaultsMutex_);
        defaults = defaults_;
    }
    if (defaults.empty()) {
        return;
    }
    for (const auto& pair : defaults) {
        storage_->addIfAbsent(chatId, pair.first, pair.second);
    }
}

MatchResult RpService::match(int64_t chatId, int64_t userId, const std::string& text,
                             int64_t replyToUserId, const std::string& mention1,
                             const std::string& mention2) {
    MatchResult result;
    ensureDefaults(chatId);

    const std::string trigger = extractTrigger(text);
    if (trigger.empty()) {
        return result;
    }

    auto stored = storage_->getCommand(chatId, normalizeTrigger(trigger));
    if (!stored) {
        return result;
    }

    std::string m1 = mention1.empty() ? std::to_string(userId) : mention1;
    std::string m2 = mention2;
    if (m2.empty() && replyToUserId > 0) {
        m2 = std::to_string(replyToUserId);
    }

    result.matched = true;
    result.trigger = trigger;
    result.response = substituteVariables(*stored, m1, m2);
    return result;
}

CommandResult RpService::addCommand(int64_t chatId, const std::string& trigger,
                                    const std::string& response) {
    const std::string t = normalizeTrigger(trigger);
    const std::string r = trim(response);
    if (!isValidTrigger(t)) {
        return {false, "Некорректный триггер. Триггер должен быть одним словом (не длиннее " +
                           std::to_string(kMaxTriggerLength) + " символов)."};
    }
    if (!isValidResponse(r)) {
        return {false, "Некорректный ответ. Ответ не должен быть пустым (макс. " +
                           std::to_string(kMaxResponseLength) + " символов)."};
    }
    ensureDefaults(chatId);
    if (storage_->hasCommand(chatId, t)) {
        return {false, "Команда уже существует. Используйте действие edit для изменения."};
    }
    if (!storage_->addCommand(chatId, t, r)) {
        return {false, "Не удалось сохранить команду."};
    }
    return {true, "Команда добавлена."};
}

CommandResult RpService::removeCommand(int64_t chatId, const std::string& trigger) {
    const std::string t = normalizeTrigger(trigger);
    if (!isValidTrigger(t)) {
        return {false, "Некорректный триггер."};
    }
    if (!storage_->removeCommand(chatId, t)) {
        return {false, "Команда не найдена."};
    }
    return {true, "Команда удалена."};
}

CommandResult RpService::editCommand(int64_t chatId, const std::string& trigger,
                                     const std::string& response) {
    const std::string t = normalizeTrigger(trigger);
    const std::string r = trim(response);
    if (!isValidTrigger(t)) {
        return {false, "Некорректный триггер."};
    }
    if (!isValidResponse(r)) {
        return {false, "Некорректный ответ."};
    }
    if (!storage_->hasCommand(chatId, t)) {
        return {false, "Команда не найдена. Используйте действие add для создания."};
    }
    if (!storage_->updateCommand(chatId, t, r)) {
        return {false, "Не удалось обновить команду."};
    }
    return {true, "Команда обновлена."};
}

std::unordered_map<std::string, std::string> RpService::listCommands(int64_t chatId) {
    ensureDefaults(chatId);
    return storage_->listCommands(chatId);
}

}  // namespace rp
