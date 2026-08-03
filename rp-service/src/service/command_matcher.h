#ifndef PIBOT_RP_COMMAND_MATCHER_H
#define PIBOT_RP_COMMAND_MATCHER_H

#include <cctype>
#include <string>

namespace rp {

// Extracts the trigger from a message: the first whitespace-delimited word.
// Returns an empty string when the text is empty or contains only whitespace.
inline std::string extractTrigger(const std::string& text) {
    size_t start = 0;
    while (start < text.size() &&
           std::isspace(static_cast<unsigned char>(text[start]))) {
        ++start;
    }
    size_t end = start;
    while (end < text.size() &&
           !std::isspace(static_cast<unsigned char>(text[end]))) {
        ++end;
    }
    return text.substr(start, end - start);
}

}  // namespace rp

#endif  // PIBOT_RP_COMMAND_MATCHER_H
