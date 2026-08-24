#include "utils/helpers.h"

#include <cctype>
#include <cstdlib>
#include <iterator>

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

std::string escapeMarkdown(const std::string& text) {
    std::string out;
    out.reserve(text.size() + text.size() / 4);
    for (char c : text) {
        switch (c) {
            case '\\':
            case '_':
            case '*':
            case '`':
            case '[':
                out.push_back('\\');
                break;
            default:
                break;
        }
        out.push_back(c);
    }
    return out;
}

std::string mentionUser(int64_t userId) {
    return "[user " + std::to_string(userId) + "](tg://user?id=" + std::to_string(userId) + ")";
}

std::string mentionUser(int64_t userId, const std::string& name) {
    std::string escaped;
    escaped.reserve(name.size());
    for (char c : name) {
        if (c == '\\' || c == '`' || c == '*' || c == '_' || c == '[') {
            escaped += '\\';
        }
        escaped += c;
    }
    return "[" + escaped + "](tg://user?id=" + std::to_string(userId) + ")";
}

namespace {

constexpr uint32_t kInvalidCodePoint = 0xFFFFFFFF;

// Decodes one UTF-8 code point starting at s[i] and advances i past its
// bytes. Returns kInvalidCodePoint for malformed sequences (the bytes are
// still consumed so decoding always makes progress).
uint32_t decodeUtf8Point(const std::string& s, size_t& i) {
    const auto lead = static_cast<unsigned char>(s[i]);
    size_t len = 0;
    uint32_t cp = 0;
    if (lead < 0x80) {
        ++i;
        return lead;
    }
    if ((lead & 0xE0) == 0xC0) {
        len = 2;
        cp = lead & 0x1F;
    } else if ((lead & 0xF0) == 0xE0) {
        len = 3;
        cp = lead & 0x0F;
    } else if ((lead & 0xF8) == 0xF0) {
        len = 4;
        cp = lead & 0x07;
    } else {
        ++i;  // stray continuation byte or invalid lead
        return kInvalidCodePoint;
    }
    if (i + len > s.size()) {
        i = s.size();
        return kInvalidCodePoint;
    }
    for (size_t j = 1; j < len; ++j) {
        const auto cont = static_cast<unsigned char>(s[i + j]);
        if ((cont & 0xC0) != 0x80) {
            i += j;
            return kInvalidCodePoint;
        }
        cp = (cp << 6) | (cont & 0x3F);
    }
    i += len;
    return cp;
}

void encodeUtf8Point(std::string& out, uint32_t cp) {
    if (cp < 0x80) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

// Inclusive UTF-8 code point range.
struct CodePointRange {
    uint32_t first;
    uint32_t last;
};

// Combining marks — the zalgo source: glyphs stacked onto the preceding
// character. Regular letters, punctuation and emoji are never in these.
constexpr CodePointRange kZalgoRanges[] = {
    {0x0300, 0x036F},   // combining diacritical marks
    {0x0483, 0x0489},   // cyrillic combining
    {0x0591, 0x05C7},   // hebrew points
    {0x0610, 0x061A},   // arabic marks
    {0x064B, 0x065F},   // arabic diacritics
    {0x0670, 0x0670},   // arabic letter mark
    {0x06D6, 0x06ED},   // arabic formatting
    {0x0730, 0x074A},   // syriac marks
    {0x07EB, 0x07F3},   // nko marks
    {0x135D, 0x135F},   // ethiopic combining
    {0x1AB0, 0x1AFF},   // diacritics extended
    {0x1DC0, 0x1DFF},   // diacritics supplement
    {0x20D0, 0x20F0},   // combining marks for symbols
    {0xFE20, 0xFE2F},   // combining half marks
};

// Invisible formatting and bidi controls that break plain text.
constexpr CodePointRange kBreakingRanges[] = {
    {0x200B, 0x200F},    // zero-width spaces + bidi marks
    {0x2028, 0x202E},    // line/paragraph separators + bidi overrides
    {0x2060, 0x2064},    // invisible operators
    {0x2066, 0x206F},    // bidi isolates + deprecated format chars
    {0xFE00, 0xFE0F},    // variation selectors
    {0xFEFF, 0xFEFF},    // zero-width no-break space (BOM)
    {0xFFF9, 0xFFFB},    // interlinear annotation anchors
    {0x1D173, 0x1D17A},  // musical formatting controls
    {0xE0000, 0xE007F},  // tags
    {0xE0100, 0xE01EF},  // variation selectors supplement
};

bool inRanges(uint32_t cp, const CodePointRange* ranges, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        if (cp >= ranges[i].first && cp <= ranges[i].last) {
            return true;
        }
    }
    return false;
}

// Control characters, keeping newlines only.
bool isControlChar(uint32_t cp) {
    if (cp == static_cast<uint32_t>('\n')) {
        return false;
    }
    return cp < 0x20 || cp == 0x7F || (cp >= 0x80 && cp <= 0x9F);
}

bool isZalgoOrBreaking(uint32_t cp) {
    return isControlChar(cp) || inRanges(cp, kZalgoRanges, std::size(kZalgoRanges)) ||
           inRanges(cp, kBreakingRanges, std::size(kBreakingRanges));
}

}  // namespace

std::string sanitizeForAi(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    size_t i = 0;
    while (i < text.size()) {
        const uint32_t cp = decodeUtf8Point(text, i);
        if (cp != kInvalidCodePoint && !isZalgoOrBreaking(cp)) {
            encodeUtf8Point(out, cp);
        }
    }
    return out;
}

std::string truncateUtf8(const std::string& text, size_t maxBytes) {
    if (text.size() <= maxBytes) {
        return text;
    }
    // Step back over the continuation bytes of the character straddling the
    // cut, so the result ends on a code point boundary. The straddling
    // character's lead byte lands on the cut itself and is excluded below.
    size_t cut = maxBytes;
    while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xC0) == 0x80) {
        --cut;
    }
    return text.substr(0, cut);
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
