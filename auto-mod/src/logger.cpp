#include "logger.h"

#include <chrono>
#include <ctime>
#include <memory>

#include <spdlog/formatter.h>
#include <spdlog/sinks/stdout_color_sinks.h>

namespace {

void appendStr(spdlog::memory_buf_t& dest, const char* s) {
    dest.append(spdlog::string_view_t(s));
}

void appendEscaped(spdlog::string_view_t input, spdlog::memory_buf_t& dest) {
    dest.push_back('"');
    for (size_t i = 0; i < input.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(input[i]);
        switch (c) {
            case '"': dest.append(spdlog::string_view_t("\\\"", 2)); break;
            case '\\': dest.append(spdlog::string_view_t("\\\\", 2)); break;
            case '\n': dest.append(spdlog::string_view_t("\\n", 2)); break;
            case '\r': dest.append(spdlog::string_view_t("\\r", 2)); break;
            case '\t': dest.append(spdlog::string_view_t("\\t", 2)); break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    dest.append(spdlog::string_view_t(buf, 6));
                } else {
                    dest.push_back(c);
                }
        }
    }
    dest.push_back('"');
}

// Serializes each log record as a single JSON line:
//   {"ts":..., "level":..., "logger":..., ...payload}
// A payload starting with the record separator byte (\x1e) is treated as
// already-structured JSON and embedded verbatim.
class JsonFormatter : public spdlog::formatter {
public:
    void format(const spdlog::details::log_msg& msg,
                spdlog::memory_buf_t& dest) override {
        char ts[40];
        std::time_t t = std::chrono::system_clock::to_time_t(msg.time);
        std::tm tm{};
        gmtime_r(&t, &tm);
        std::strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%SZ", &tm);

        appendStr(dest, "{\"ts\":\"");
        appendStr(dest, ts);
        appendStr(dest, "\",\"level\":\"");
        spdlog::string_view_t level = spdlog::level::to_string_view(msg.level);
        dest.append(level);
        appendStr(dest, "\",\"logger\":\"");
        dest.append(spdlog::string_view_t(msg.logger_name.data(), msg.logger_name.size()));
        appendStr(dest, "\",");

        spdlog::string_view_t payload(msg.payload.data(), msg.payload.size());
        if (payload.size() > 0 && payload[0] == '\x1e') {
            dest.append(spdlog::string_view_t(payload.data() + 1, payload.size() - 1));
        } else {
            appendStr(dest, "\"message\":");
            appendEscaped(payload, dest);
        }
        appendStr(dest, "}\n");
    }

    std::unique_ptr<spdlog::formatter> clone() const override {
        return std::make_unique<JsonFormatter>();
    }
};

}  // namespace

void Logger::init(spdlog::level::level_enum level) {
    auto sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    sink->set_formatter(std::make_unique<JsonFormatter>());
    auto logger = std::make_shared<spdlog::logger>("auto-mod", sink);
    logger->set_level(level);
    spdlog::set_default_logger(logger);
}

std::shared_ptr<spdlog::logger> Logger::get() {
    return spdlog::default_logger();
}

void Logger::info(const std::string& message) {
    get()->log(spdlog::level::info, message);
}

void Logger::warn(const std::string& message) {
    get()->log(spdlog::level::warn, message);
}

void Logger::error(const std::string& message) {
    get()->log(spdlog::level::err, message);
}
