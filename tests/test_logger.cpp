#include <gtest/gtest.h>

#include <spdlog/sinks/base_sink.h>
#include <spdlog/spdlog.h>

#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "logger.h"

// Pull in the implementation so the file-local JsonFormatter
// (anonymous namespace in logger.cpp) is directly constructible here.
#include "../auto-mod/src/logger.cpp"

namespace {

class JsonFormatterTest : public ::testing::Test {
protected:
    std::string format(spdlog::level::level_enum level,
                       const std::string& payload) {
        spdlog::details::log_msg msg;
        msg.logger_name = spdlog::string_view_t("auto-mod");
        msg.level = level;
        msg.time = spdlog::log_clock::now();
        msg.payload = spdlog::string_view_t(payload.data(), payload.size());

        spdlog::memory_buf_t dest;
        formatter_.format(msg, dest);
        return std::string(dest.data(), dest.size());
    }

    // Extracts the value of `"key":"..."` from a formatted line.
    static std::string string_field(const std::string& line,
                                    const std::string& key) {
        const std::string needle = "\"" + key + "\":\"";
        const size_t start = line.find(needle);
        EXPECT_NE(start, std::string::npos) << line;
        if (start == std::string::npos) {
            return "";
        }
        const size_t value_start = start + needle.size();
        std::string out;
        size_t i = value_start;
        while (i < line.size()) {
            if (line[i] == '\\' && i + 1 < line.size()) {
                // Keep escape sequences (e.g. \" or \\) verbatim.
                out += line[i];
                out += line[i + 1];
                i += 2;
                continue;
            }
            if (line[i] == '"') {
                break;
            }
            out += line[i];
            ++i;
        }
        return out;
    }

    JsonFormatter formatter_;
};

TEST_F(JsonFormatterTest, PlainPayloadBecomesSingleJsonLine) {
    const std::string line = format(spdlog::level::info, "hello");

    ASSERT_EQ(line.front(), '{');
    ASSERT_EQ(line.back(), '\n');
    EXPECT_TRUE(line.find("\"ts\":\"") != std::string::npos) << line;
    EXPECT_EQ(string_field(line, "level"), "info");
    EXPECT_EQ(string_field(line, "logger"), "auto-mod");
    EXPECT_EQ(string_field(line, "message"), "hello");

    // Exactly one trailing newline, no embedded ones for this payload.
    EXPECT_EQ(line.find('\n'), line.size() - 1);
}

TEST_F(JsonFormatterTest, TimestampIsIso8601Utc) {
    const std::string line = format(spdlog::level::info, "t");
    const std::string ts = string_field(line, "ts");

    // YYYY-MM-DDTHH:MM:SSZ
    ASSERT_EQ(ts.size(), 20u);
    EXPECT_EQ(ts[4], '-');
    EXPECT_EQ(ts[7], '-');
    EXPECT_EQ(ts[10], 'T');
    EXPECT_EQ(ts[13], ':');
    EXPECT_EQ(ts[16], ':');
    EXPECT_EQ(ts.back(), 'Z');
    for (size_t idx : {0u, 1u, 2u, 3u, 5u, 6u, 8u, 9u, 11u, 12u, 14u, 15u,
                       17u, 18u}) {
        EXPECT_TRUE(isdigit(static_cast<unsigned char>(ts[idx]))) << ts;
    }
}

TEST_F(JsonFormatterTest, SpecialCharactersAreEscaped) {
    const std::string line =
        format(spdlog::level::info, std::string("a\"b\\c\nd\te\rf\x01g"));

    EXPECT_EQ(string_field(line, "message"),
              "a\\\"b\\\\c\\nd\\te\\rf\\u0001g");
    // The escaped newline must stay literal (two chars), not break the line.
    EXPECT_EQ(line.find('\n'), line.size() - 1);
}

TEST_F(JsonFormatterTest, Utf8PayloadPassesThroughUnescaped) {
    const std::string line = format(spdlog::level::info, "привет мир");
    EXPECT_EQ(string_field(line, "message"), "привет мир");
}

TEST_F(JsonFormatterTest, RecordSeparatorPrefixEmbedsPayloadVerbatim) {
    const std::string line =
        format(spdlog::level::info,
               std::string("\x1e\"event\":\"x\",\"n\":5,\"s\":\"a b\""));

    // Pre-structured payload is appended raw, no "message" re-escaping.
    EXPECT_TRUE(line.find("\"event\":\"x\",\"n\":5,\"s\":\"a b\"") !=
                std::string::npos)
        << line;
    EXPECT_TRUE(line.find("\"message\":") == std::string::npos) << line;
    // Only the separator byte itself is stripped.
    EXPECT_TRUE(line.substr(0, line.find("\"event\"")).find("\x1e") ==
                std::string::npos)
        << line;
}

TEST_F(JsonFormatterTest, LevelsRenderWithSpdlogNames) {
    EXPECT_NE(format(spdlog::level::warn, "w").find("\"level\":\"warning\""),
              std::string::npos);
    EXPECT_NE(format(spdlog::level::err, "e").find("\"level\":\"error\""),
              std::string::npos);
    EXPECT_NE(format(spdlog::level::debug, "d").find("\"level\":\"debug\""),
              std::string::npos);
    EXPECT_NE(format(spdlog::level::critical, "c")
                  .find("\"level\":\"critical\""),
              std::string::npos);
}

TEST_F(JsonFormatterTest, CloneProducesWorkingCopy) {
    const auto copy = formatter_.clone();
    ASSERT_NE(copy, nullptr);

    spdlog::details::log_msg msg;
    msg.logger_name = spdlog::string_view_t("auto-mod");
    msg.level = spdlog::level::info;
    msg.time = spdlog::log_clock::now();
    msg.payload = spdlog::string_view_t("cloned", 6);

    spdlog::memory_buf_t dest;
    copy->format(msg, dest);
    const std::string line(dest.data(), dest.size());
    EXPECT_EQ(string_field(line, "message"), "cloned");
}

// End-to-end through the Logger facade with a rerouted sink.
class CaptureSink : public spdlog::sinks::base_sink<std::mutex> {
public:
    CaptureSink() { set_formatter(std::make_unique<JsonFormatter>()); }
    const std::vector<std::string>& lines() const { return lines_; }

protected:
    void sink_it_(const spdlog::details::log_msg& msg) override {
        spdlog::memory_buf_t formatted;
        formatter_->format(msg, formatted);
        lines_.emplace_back(formatted.data(), formatted.size());
    }

    void flush_() override {}

private:
    std::vector<std::string> lines_;
};

class LoggerApiTest : public ::testing::Test {
protected:
    void SetUp(spdlog::level::level_enum level) {
        Logger::init(level);
        auto default_logger = spdlog::default_logger();
        capture_ = std::make_shared<CaptureSink>();
        default_logger->sinks().clear();
        default_logger->sinks().push_back(capture_);
    }

    const std::string& last_line() const { return capture_->lines().back(); }

    std::shared_ptr<CaptureSink> capture_;
};

TEST_F(LoggerApiTest, InfoWarnErrorRouteThroughDefaultLogger) {
    SetUp(spdlog::level::debug);

    Logger::info("i");
    ASSERT_EQ(capture_->lines().size(), 1u);
    EXPECT_TRUE(last_line().find("\"level\":\"info\"") != std::string::npos);

    Logger::warn("w");
    ASSERT_EQ(capture_->lines().size(), 2u);
    EXPECT_TRUE(last_line().find("\"level\":\"warning\"") != std::string::npos);

    Logger::error("e");
    ASSERT_EQ(capture_->lines().size(), 3u);
    EXPECT_TRUE(last_line().find("\"level\":\"error\"") != std::string::npos);
}

TEST_F(LoggerApiTest, InitLevelFiltersBelowThreshold) {
    SetUp(spdlog::level::warn);

    Logger::info("hidden");
    Logger::warn("shown");

    ASSERT_EQ(capture_->lines().size(), 1u);
    EXPECT_TRUE(last_line().find("\"message\":\"shown\"") != std::string::npos)
        << last_line();
}

TEST_F(LoggerApiTest, GetReturnsInitializedNamedLogger) {
    SetUp(spdlog::level::info);
    EXPECT_EQ(Logger::get(), spdlog::default_logger());
    EXPECT_STREQ(Logger::get()->name().c_str(), "auto-mod");
}

}  // namespace
