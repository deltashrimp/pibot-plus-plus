#include <gtest/gtest.h>

#include <chrono>
#include <ctime>
#include <optional>
#include <string>
#include <vector>

#include "utils/helpers.h"

namespace {

using helpers::escapeMarkdown;
using helpers::mentionUser;
using helpers::parseDuration;
using helpers::parseInt;
using helpers::parseLogLevel;
using helpers::splitCommand;
using helpers::unixNow;

TEST(SplitCommandTest, EmptyStringYieldsNoTokens) {
    EXPECT_TRUE(splitCommand("").empty());
}

TEST(SplitCommandTest, WhitespaceOnlyYieldsNoTokens) {
    EXPECT_TRUE(splitCommand(" \t\n  ").empty());
}

TEST(SplitCommandTest, SplitsOnAnyWhitespaceRun) {
    const auto parts = splitCommand("/mute 42\t10m\nreason here");
    ASSERT_EQ(parts.size(), 5u);
    EXPECT_EQ(parts[0], "/mute");
    EXPECT_EQ(parts[1], "42");
    EXPECT_EQ(parts[2], "10m");
    EXPECT_EQ(parts[3], "reason");
    EXPECT_EQ(parts[4], "here");
}

TEST(SplitCommandTest, TrimsLeadingAndTrailingWhitespace) {
    const auto parts = splitCommand("   /ban   spammer   ");
    ASSERT_EQ(parts.size(), 2u);
    EXPECT_EQ(parts[0], "/ban");
    EXPECT_EQ(parts[1], "spammer");
}

TEST(ParseDurationTest, EmptyIsInvalid) {
    EXPECT_FALSE(parseDuration("").has_value());
}

TEST(ParseDurationTest, BareNumberDefaultsToMinutes) {
    EXPECT_EQ(parseDuration("30"), std::chrono::seconds(1800));
    EXPECT_EQ(parseDuration("0"), std::chrono::seconds(0));
}

TEST(ParseDurationTest, SupportedSuffixes) {
    EXPECT_EQ(parseDuration("45s"), std::chrono::seconds(45));
    EXPECT_EQ(parseDuration("5m"), std::chrono::seconds(300));
    EXPECT_EQ(parseDuration("2h"), std::chrono::seconds(7200));
    EXPECT_EQ(parseDuration("3d"), std::chrono::seconds(259200));
    EXPECT_EQ(parseDuration("1w"), std::chrono::seconds(604800));
    EXPECT_EQ(parseDuration("1M"), std::chrono::seconds(2592000));
    EXPECT_EQ(parseDuration("1y"), std::chrono::seconds(31536000));
}

TEST(ParseDurationTest, MinuteAndMonthSuffixesAreCaseSensitive) {
    EXPECT_NE(parseDuration("5M").value().count(), parseDuration("5m").value().count());
}

TEST(ParseDurationTest, NonNumericPrefixIsInvalid) {
    EXPECT_FALSE(parseDuration("abc").has_value());
    EXPECT_FALSE(parseDuration("m5").has_value());
}

TEST(ParseDurationTest, UnknownSuffixIsInvalid) {
    EXPECT_FALSE(parseDuration("5x").has_value());
}

TEST(ParseDurationTest, TrailingGarbageAfterSuffixIsInvalid) {
    EXPECT_FALSE(parseDuration("5m3").has_value());
    EXPECT_FALSE(parseDuration("5mm").has_value());
}

TEST(EscapeMarkdownTest, EmptyStaysEmpty) {
    EXPECT_EQ(escapeMarkdown(""), "");
}

TEST(EscapeMarkdownTest, EscapesAllSpecialCharacters) {
    EXPECT_EQ(escapeMarkdown("a_b*c`d[e\\f"),
              "a\\_b\\*c\\`d\\[e\\\\f");
}

TEST(EscapeMarkdownTest, LeavesPlainTextUntouched) {
    EXPECT_EQ(escapeMarkdown("hello, world! (ok)"), "hello, world! (ok)");
}

TEST(MentionUserTest, IdOnlyFormat) {
    EXPECT_EQ(mentionUser(42), "[user 42](tg://user?id=42)");
    EXPECT_EQ(mentionUser(-1), "[user -1](tg://user?id=-1)");
}

TEST(MentionUserTest, NamedMentionEscapesName) {
    EXPECT_EQ(mentionUser(7, "Иван_Б*"),
              "[Иван\\_Б\\*](tg://user?id=7)");
}

TEST(ParseLogLevelTest, KnownNamesMapToLevels) {
    EXPECT_EQ(parseLogLevel("debug"), spdlog::level::debug);
    EXPECT_EQ(parseLogLevel("warn"), spdlog::level::warn);
    EXPECT_EQ(parseLogLevel("warning"), spdlog::level::warn);
    EXPECT_EQ(parseLogLevel("error"), spdlog::level::err);
    EXPECT_EQ(parseLogLevel("critical"), spdlog::level::critical);
}

TEST(ParseLogLevelTest, UnknownNamesFallBackToInfo) {
    EXPECT_EQ(parseLogLevel(""), spdlog::level::info);
    EXPECT_EQ(parseLogLevel("VERBOSE"), spdlog::level::info);
    EXPECT_EQ(parseLogLevel("inf"), spdlog::level::info);
}

TEST(ParseIntTest, NullOrEmptyFallsBack) {
    EXPECT_EQ(parseInt(nullptr, 5), 5);
    EXPECT_EQ(parseInt("", 5), 5);
}

TEST(ParseIntTest, ValidNumberParses) {
    EXPECT_EQ(parseInt("42", 5), 42);
    EXPECT_EQ(parseInt("-7", 5), -7);
    EXPECT_EQ(parseInt("0", 5), 0);
}

TEST(ParseIntTest, PartialOrNonNumericFallsBack) {
    EXPECT_EQ(parseInt("4x2", 5), 5);
    EXPECT_EQ(parseInt("abc", 5), 5);
}

TEST(UnixNowTest, MatchesSystemClockWithinTolerance) {
    const auto now = static_cast<int64_t>(std::time(nullptr));
    const int64_t got = unixNow();
    EXPECT_GE(got, now - 5);
    EXPECT_LE(got, now + 5);
}

}  // namespace
