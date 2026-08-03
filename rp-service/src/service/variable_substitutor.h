#ifndef PIBOT_RP_VARIABLE_SUBSTITUTOR_H
#define PIBOT_RP_VARIABLE_SUBSTITUTOR_H

#include <string>

namespace rp {

// Replaces every occurrence of `from` with `to` in `text`.
inline std::string replaceAll(std::string text, const std::string& from,
                              const std::string& to) {
    if (from.empty()) {
        return text;
    }
    size_t pos = 0;
    while ((pos = text.find(from, pos)) != std::string::npos) {
        text.replace(pos, from.size(), to);
        pos += to.size();
    }
    return text;
}

// Substitutes placeholders in a response template:
//   {mention}  and {mention1} -> `mention`  (the user who triggered the command)
//   {mention2}                -> `mention2` (the mentioned/replied-to user)
inline std::string substituteVariables(const std::string& text,
                                       const std::string& mention,
                                       const std::string& mention2) {
    std::string out = replaceAll(text, "{mention1}", mention);
    out = replaceAll(out, "{mention}", mention);
    out = replaceAll(out, "{mention2}", mention2);
    return out;
}

}  // namespace rp

#endif  // PIBOT_RP_VARIABLE_SUBSTITUTOR_H
