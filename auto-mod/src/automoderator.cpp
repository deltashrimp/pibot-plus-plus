#include "automoderator.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <string_view>

#include <spdlog/spdlog.h>

namespace {

// Hard-coded forbidden phrases, identical to the Python `FILTERS` list.
// All entries are already lower-case; they are matched case-insensitively
// against the message text (see `to_lower_utf8`).
constexpr std::array<std::string_view, 11> kFilterPatterns = {
    "заработай в телеграм", "пассивный доход", "вложение от",
    "гарантия дохода",      "нужны люди для",  "ссылка в био",
    "пиши в лс",            "удалённая работа", "доход в день",
    "вы плывете",           "консультация бесплатно",
};

// Anti-spam tuning: sustained sending above 3 messages/second for 3 seconds
// (i.e. more than 9 messages in a 3-second sliding window) triggers a mute.
constexpr double kSpamWindowSeconds = 3.0;  // 3-second sliding window
constexpr size_t kSpamLimit = 9;            // 3 msg/s sustained for 3 s => spam
constexpr int kMuteDurationSeconds = 60;    // temporary mute length

// GC: when the total number of tracked timestamps exceeds this bound, sweep
// the table and drop everything outside the spam window. Keeps memory bounded
// without doing work on every single call.
constexpr size_t kTrackedCap = 4096;

// Lower-case fold of an upper-case Cyrillic letter encoded as a 0xD0-prefixed
// two-byte UTF-8 sequence with continuation byte c2. Appends the fold to out
// and returns true, or leaves out untouched and returns false when the pair is
// not А-Я/Ё.
bool append_lower_cyrillic(std::string& out, unsigned char c2) {
    if (c2 >= 0x90 && c2 <= 0xAF) {
        // U+0410..U+042F (А-Я) -> U+0430..U+044F (а-я).
        if (c2 <= 0x9F) {
            // а-п: 0xD0 0xB0..0xBF.
            out.push_back(static_cast<char>(0xD0));
            out.push_back(static_cast<char>(c2 + 0x20));
        } else {
            // р-я: 0xD1 0x80..0x8F.
            out.push_back(static_cast<char>(0xD1));
            out.push_back(static_cast<char>(c2 - 0x20));
        }
        return true;
    }
    if (c2 == 0x81) {
        // Ё (U+0401) -> ё (U+0451).
        out.push_back(static_cast<char>(0xD1));
        out.push_back(static_cast<char>(0x91));
        return true;
    }
    return false;
}

// Fold the text to lower case. The result is byte-identical for lower-case
// input, so repeated folding is harmless.
//
// Handles ASCII (A-Z) and Cyrillic uppercase (А-Я, Ё) which is enough for the
// hard-coded Russian patterns. All other bytes are passed through unchanged.
std::string to_lower_utf8(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(s[i]);

        if (c >= 0x41 && c <= 0x5A) {
            // ASCII uppercase letter: A-Z -> a-z.
            out.push_back(static_cast<char>(c + 0x20));
            continue;
        }

        if (c == 0xD0 && i + 1 < s.size()) {
            // Two-byte UTF-8 sequence, Cyrillic range.
            unsigned char c2 = static_cast<unsigned char>(s[i + 1]);
            if (append_lower_cyrillic(out, c2)) {
                ++i;
                continue;
            }
            out.push_back(static_cast<char>(c));
            out.push_back(static_cast<char>(c2));
            ++i;
            continue;
        }

        out.push_back(static_cast<char>(c));
    }
    return out;
}

// JSON escape sequence per byte value, nullptr when the byte needs no escape.
constexpr std::array<const char*, 256> kJsonEscapes = [] {
    std::array<const char*, 256> table{};
    table[static_cast<unsigned char>('"')] = "\\\"";
    table[static_cast<unsigned char>('\\')] = "\\\\";
    table[static_cast<unsigned char>('\n')] = "\\n";
    table[static_cast<unsigned char>('\r')] = "\\r";
    table[static_cast<unsigned char>('\t')] = "\\t";
    return table;
}();

// Append one byte of a string in its JSON string-literal form to out:
// named escapes for the special characters, \u00XX for other control bytes,
// the byte itself otherwise.
void append_json_escaped(std::string& out, unsigned char c) {
    if (const char* escaped = kJsonEscapes[c]) {
        out += escaped;
        return;
    }
    if (c >= 0x20) {
        out.push_back(static_cast<char>(c));
        return;
    }
    char buf[8];
    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
    out += buf;
}

// Escape a string for inclusion inside a JSON string literal.
std::string json_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 2);
    out.push_back('"');
    for (char ch : s) {
        append_json_escaped(out, static_cast<unsigned char>(ch));
    }
    out.push_back('"');
    return out;
}

// First `max_bytes` bytes of the text, cut at a UTF-8 character boundary so
// multi-byte (e.g. Cyrillic) characters are never split in the log.
std::string message_preview(const std::string& text, size_t max_bytes = 50) {
    if (text.size() <= max_bytes) {
        return text;
    }
    size_t end = max_bytes;
    while (end > 0 && (static_cast<unsigned char>(text[end]) & 0xC0) == 0x80) {
        --end;
    }
    return text.substr(0, end);
}

// Emit a structured JSON log line for every decision. The payload starts with
// a record separator (\x1e) to tell the logger's JSON formatter that the text
// is already structured and must not be re-escaped.
void log_decision_impl(int64_t chat_id, int64_t user_id,
                       const std::string& text, const Action& action,
                       const std::string& matched_pattern,
                       const std::string& skip_reason) {
    std::string payload = "\x1e";
    payload += "\"event\":\"moderation_decision\"";
    payload += ",\"chat_id\":" + std::to_string(chat_id);
    payload += ",\"user_id\":" + std::to_string(user_id);
    payload += ",\"action\":\"";
    payload += action_type_to_string(action.type);
    payload += "\"";
    payload += ",\"delete_message\":";
    payload += action.delete_message ? "true" : "false";
    payload += ",\"message_preview\":";
    payload += json_escape(message_preview(text));
    if (!matched_pattern.empty()) {
        payload += ",\"matched_pattern\":";
        payload += json_escape(matched_pattern);
    }
    if (!skip_reason.empty()) {
        payload += ",\"skip_reason\":\"";
        payload += skip_reason;
        payload += "\"";
    }
    auto logger = spdlog::default_logger();
    if (action.type == ActionType::Allow) {
        logger->log(spdlog::level::info, payload);
    } else {
        logger->log(spdlog::level::warn, payload);
    }
}

void log_decision(int64_t chat_id, int64_t user_id, const std::string& text,
                  const Action& action, const std::string& matched_pattern) {
    log_decision_impl(chat_id, user_id, text, action, matched_pattern, "");
}

}  // namespace

const char* action_type_to_string(ActionType type) {
    switch (type) {
        case ActionType::Allow: return "Allow";
        case ActionType::MuteTemporary: return "MuteTemporary";
        case ActionType::MutePermanent: return "MutePermanent";
    }
    return "Allow";
}

Action AutoModerator::process_message(int64_t chat_id, int64_t user_id,
                                      const std::string& text,
                                      double timestamp) {
    // 1. Keyword filter. Checked before anti-spam: a filtered message is
    //    never counted towards the spam tracker.
    std::string matched = match_filter(to_lower_utf8(text));
    if (!matched.empty()) {
        // The caller deletes the message and applies a permanent mute.
        Action action{ActionType::MutePermanent, 0, true};
        log_decision(chat_id, user_id, text, action, matched);
        return action;
    }

    // 2. Anti-spam. Only messages that pass the filter reach this point.
    Action action;
    {
        std::lock_guard<std::mutex> lock(mutex_);

        const double cutoff = timestamp - kSpamWindowSeconds;

        auto& chat_tracker = spam_trackers_[chat_id];
        auto& user_tracker = chat_tracker[user_id];

        // Keep only timestamps inside the 3-second sliding window
        // (t > now - window), so a muted rate is only detected once the
        // flood has continued for 3 seconds.
        user_tracker.erase(
            std::remove_if(user_tracker.begin(), user_tracker.end(),
                           [cutoff](double t) { return t <= cutoff; }),
            user_tracker.end());
        user_tracker.push_back(timestamp);
        ++tracked_count_;

        if (user_tracker.size() <= kSpamLimit) {
            action = {ActionType::Allow, 0, false};
        } else {
            // Sustained >3 messages/second for 3 seconds: temporary mute.
            prune_locked(timestamp);
            action = {ActionType::MuteTemporary, kMuteDurationSeconds, false};
        }
    }

    log_decision(chat_id, user_id, text, action, "");
    return action;
}

Action AutoModerator::skip_privileged(int64_t chat_id, int64_t user_id,
                                      const std::string& text) {
    Action action{ActionType::Allow, 0, false};
    log_decision_impl(chat_id, user_id, text, action, "", "privileged");
    return action;
}

std::string AutoModerator::match_filter(const std::string& lower_text) const {
    for (std::string_view pattern : kFilterPatterns) {
        if (lower_text.find(pattern) != std::string::npos) {
            return std::string(pattern);
        }
    }
    return std::string();
}

void AutoModerator::prune_locked(double now) {
    if (tracked_count_ <= kTrackedCap) {
        return;
    }

    const double cutoff = now - kSpamWindowSeconds;
    for (auto chat = spam_trackers_.begin(); chat != spam_trackers_.end();) {
        for (auto user = chat->second.begin(); user != chat->second.end();) {
            auto& timestamps = user->second;
            timestamps.erase(
                std::remove_if(timestamps.begin(), timestamps.end(),
                               [cutoff](double t) { return t <= cutoff; }),
                timestamps.end());
            if (timestamps.empty()) {
                user = chat->second.erase(user);
            } else {
                ++user;
            }
        }
        if (chat->second.empty()) {
            chat = spam_trackers_.erase(chat);
        } else {
            ++chat;
        }
    }
    tracked_count_ = 0;
    for (const auto& chat : spam_trackers_) {
        for (const auto& user : chat.second) {
            tracked_count_ += user.second.size();
        }
    }
}
