#include <gtest/gtest.h>

#include <spdlog/spdlog.h>

#include <string>

#include "automoderator.hpp"

// Pull in the implementation so file-local helpers (to_lower_utf8,
// json_escape, message_preview) are directly testable.
#include "../auto-mod/src/automoderator.cpp"

namespace {

// ---------------------------------------------------------------------------
// File-local helpers, unit-tested directly.
// ---------------------------------------------------------------------------

TEST(ToLowerUtf8Test, AsciiUppercaseFoldsToLowercase) {
    EXPECT_EQ(to_lower_utf8("HELLO World 123"), "hello world 123");
}

TEST(ToLowerUtf8Test, LowercaseAndNonLettersAreByteIdentical) {
    const std::string in = "already lower! @# 0x00";
    EXPECT_EQ(to_lower_utf8(in), in);
}

TEST(ToLowerUtf8Test, CyrillicUppercaseAFolds) {
    // А..П (0xD0 0x90..0x9F) -> а..п (0xD0 0xB0..0xBF).
    EXPECT_EQ(to_lower_utf8("АБВГДЕЖЗИЙКЛМНОП"),
              "абвгдежзийклмноп");
}

TEST(ToLowerUtf8Test, CyrillicUppercaseRFolds) {
    // Р..Я (0xD0 0xA0..0xAF) -> р..я (0xD1 0x80..0x8F).
    EXPECT_EQ(to_lower_utf8("РСТУФХЦЧШЩЪЫЬЭЮЯ"),
              "рстуфхцчшщъыьэюя");
}

TEST(ToLowerUtf8Test, YoFolds) {
    EXPECT_EQ(to_lower_utf8("Ёё"), "ёё");
    // Ё is U+0401 = D0 81; ё is U+0451 = D1 91.
    std::string out = to_lower_utf8("\xD0\x81");
    EXPECT_EQ(out, "\xD1\x91");
}

TEST(ToLowerUtf8Test, MixedCyrillicAndAscii) {
    EXPECT_EQ(to_lower_utf8("Пиши В ЛС"), "пиши в лс");
}

TEST(ToLowerUtf8Test, OtherMultibyteSequencesPassThrough) {
    // Non-Cyrillic two-byte sequences must survive untouched (incl. the
    // trailing lone-lead-byte case).
    const std::string in = "\xC3\xA9 caf\xC3\xA9 \xD0";
    EXPECT_EQ(to_lower_utf8(in), in);
}

TEST(ToLowerUtf8Test, TruncatedCyrillicLeadAtEndDoesNotOverrun) {
    // A dangling 0xD0 at end-of-string must be copied as-is.
    const std::string in = "ok \xD0";
    EXPECT_EQ(to_lower_utf8(in), in);
}

TEST(ToLowerUtf8Test, Idempotent) {
    const std::string mixed = "Смешанный ТЕКСТ ЁЁ";
    EXPECT_EQ(to_lower_utf8(to_lower_utf8(mixed)), to_lower_utf8(mixed));
}

TEST(JsonEscapeTest, WrapsInQuotes) {
    EXPECT_EQ(json_escape(""), "\"\"");
    EXPECT_EQ(json_escape("abc"), "\"abc\"");
}

TEST(JsonEscapeTest, EscapesQuoteBackslashAndControlChars) {
    EXPECT_EQ(json_escape(std::string("a\"b")), "\"a\\\"b\"");
    EXPECT_EQ(json_escape(std::string("a\\b")), "\"a\\\\b\"");
    EXPECT_EQ(json_escape(std::string("a\nb\rc\td")),
              "\"a\\nb\\rc\\td\"");
    EXPECT_EQ(json_escape(std::string("\x01\x1f")), "\"\\u0001\\u001f\"");
}

TEST(JsonEscapeTest, Utf8PassesThrough) {
    EXPECT_EQ(json_escape("привет"), "\"привет\"");
}

TEST(MessagePreviewTest, ShortTextUnchanged) {
    EXPECT_EQ(message_preview("short", 50), "short");
}

TEST(MessagePreviewTest, ExactLengthUnchanged) {
    const std::string exact(50, 'x');
    EXPECT_EQ(message_preview(exact, 50), exact);
}

TEST(MessagePreviewTest, LongAsciiIsTruncatedToLimit) {
    const std::string long_text(100, 'a');
    EXPECT_EQ(message_preview(long_text, 50).size(), 50u);
}

TEST(MessagePreviewTest, CutLandsOnUtf8Boundary) {
    // 30 Cyrillic chars = 60 bytes; limit of 50 would land mid-character.
    const std::string pair = "\xD0\xB0";  // "а"
    std::string cyr;
    cyr.reserve(60);
    for (int i = 0; i < 30; ++i) {
        cyr += pair;
    }
    ASSERT_EQ(cyr.size(), 60u);
    const std::string preview = message_preview(cyr, 50);
    EXPECT_EQ(preview.size(), 50u);
    // Cut at 50 keeps 25 complete two-byte characters.
    EXPECT_TRUE((preview.size() % 2) == 0);
    EXPECT_EQ(preview.substr(preview.size() - 2), pair);
}

TEST(MessagePreviewTest, BacktracksOffContinuationByte) {
    // Limit landing inside a character walks back to the previous boundary.
    const std::string pair = "\xD0\xB0";  // "а"
    std::string cyr;
    cyr.reserve(60);
    for (int i = 0; i < 30; ++i) {
        cyr += pair;
    }
    EXPECT_EQ(message_preview(cyr, 51).size(), 50u);
}

TEST(ActionTypeToStringTest, CoversAllActionTypes) {
    EXPECT_STREQ(action_type_to_string(ActionType::Allow), "Allow");
    EXPECT_STREQ(action_type_to_string(ActionType::MuteTemporary),
                 "MuteTemporary");
    EXPECT_STREQ(action_type_to_string(ActionType::MutePermanent),
                 "MutePermanent");
}

// ---------------------------------------------------------------------------
// AutoModerator behavior through its public API.
// ---------------------------------------------------------------------------

constexpr int kTestSpamLimit = 9;   // mirrors kSpamLimit in automoderator.cpp
constexpr int kTestMuteSeconds = 60;

class AutoModeratorTest : public ::testing::Test {
protected:
    void SetUp() override { spdlog::set_level(spdlog::level::off); }

    Action send(const std::string& text, double t, int64_t chat = 1,
                int64_t user = 1) {
        return mod_.process_message(chat, user, text, t);
    }

    AutoModerator mod_;
};

TEST_F(AutoModeratorTest, BenignMessageIsAllowed) {
    const Action a = send("hello world", 100.0);
    EXPECT_EQ(a.type, ActionType::Allow);
    EXPECT_EQ(a.duration_seconds, 0);
    EXPECT_FALSE(a.delete_message);
}

TEST_F(AutoModeratorTest, FilterHitMutesPermanentlyAndDeletes) {
    const Action a = send("срочно заработай в телеграм!!!", 100.0);
    EXPECT_EQ(a.type, ActionType::MutePermanent);
    EXPECT_EQ(a.duration_seconds, 0);
    EXPECT_TRUE(a.delete_message);
}

TEST_F(AutoModeratorTest, FilterIsCaseInsensitiveForCyrillicAndAscii) {
    EXPECT_EQ(send("Ссылка в био", 100.0).type, ActionType::MutePermanent);
    EXPECT_EQ(send("ПИШИ В ЛС", 100.0).type, ActionType::MutePermanent);
    EXPECT_EQ(send("Гарантия Дохода 100%", 100.0).type,
              ActionType::MutePermanent);
}

TEST_F(AutoModeratorTest, FilterMatchesSubstringInsideLongerText) {
    EXPECT_EQ(
        send("hey folks, хотите пассивный доход? жмите сюда", 100.0).type,
        ActionType::MutePermanent);
}

TEST_F(AutoModeratorTest, FilterCheckedBeforeSpamCounter) {
    for (int i = 0; i < kTestSpamLimit + 5; ++i) {
        const Action a = send("удалённая работа", 100.0 + i * 0.1);
        EXPECT_EQ(a.type, ActionType::MutePermanent);
        EXPECT_TRUE(a.delete_message);
    }
}

TEST_F(AutoModeratorTest, NineMessagesInWindowAreAllowedTenthMutes) {
    for (int i = 0; i < kTestSpamLimit; ++i) {
        EXPECT_EQ(send("msg", 100.0 + i * 0.1).type, ActionType::Allow);
    }
    const Action a = send("msg", 100.9);
    EXPECT_EQ(a.type, ActionType::MuteTemporary);
    EXPECT_EQ(a.duration_seconds, kTestMuteSeconds);
    EXPECT_FALSE(a.delete_message);
}

TEST_F(AutoModeratorTest, SlowMessagesNeverTriggerSpam) {
    for (int i = 0; i < 20; ++i) {
        EXPECT_EQ(send("msg", 100.0 + i * 1.5).type, ActionType::Allow)
            << "message " << i;
    }
}

TEST_F(AutoModeratorTest, WindowExpiresAndUserRecovers) {
    for (int i = 0; i <= kTestSpamLimit; ++i) {
        send("msg", 100.0 + i * 0.1);
    }
    EXPECT_EQ(send("msg", 104.5).type, ActionType::Allow);
    EXPECT_EQ(send("msg", 104.6).type, ActionType::Allow);
}

TEST_F(AutoModeratorTest, SustainedFloodingKeepsMuting) {
    for (int i = 0; i <= kTestSpamLimit; ++i) {
        send("msg", 100.0 + i * 0.1);
    }
    EXPECT_EQ(send("msg", 101.0).type, ActionType::MuteTemporary);
}

TEST_F(AutoModeratorTest, SpamTrackingIsPerUser) {
    for (int i = 0; i <= kTestSpamLimit; ++i) {
        send("msg", 100.0 + i * 0.1, /*chat=*/1, /*user=*/100);
    }
    EXPECT_EQ(send("msg", 101.0, /*chat=*/1, /*user=*/100).type,
              ActionType::MuteTemporary);
    EXPECT_EQ(send("first message", 101.0, /*chat=*/1, /*user=*/200).type,
              ActionType::Allow);
}

TEST_F(AutoModeratorTest, SpamTrackingIsPerChat) {
    for (int i = 0; i <= kTestSpamLimit; ++i) {
        send("msg", 100.0 + i * 0.1, /*chat=*/1, /*user=*/100);
    }
    EXPECT_EQ(send("msg", 101.0, /*chat=*/2, /*user=*/100).type,
              ActionType::Allow);
}

TEST_F(AutoModeratorTest, SkipPrivilegedAllowsWithoutCounting) {
    for (int i = 0; i < 30; ++i) {
        const Action a =
            mod_.skip_privileged(/*chat_id=*/1, /*user_id=*/300, "admin talk");
        EXPECT_EQ(a.type, ActionType::Allow);
        EXPECT_FALSE(a.delete_message);
    }
    EXPECT_EQ(send("regular user sees admin chatter", 100.0, 1, 400).type,
              ActionType::Allow);
    // User 300's first counted message, despite 30 prior privileged ones.
    EXPECT_EQ(mod_.process_message(1, 300, "now speaking normally", 101.0)
                  .type,
              ActionType::Allow);
}

TEST_F(AutoModeratorTest, ConfigDrivenLimitAndMuteDuration) {
    AutomodConfig cfg;
    cfg.spam_window_seconds = 1.0;
    cfg.spam_limit = 3;
    cfg.mute_duration_seconds = 120;
    AutoModerator mod(cfg);

    for (int i = 0; i < 3; ++i) {
        EXPECT_EQ(mod.process_message(1, 1, "msg", 100.0 + i * 0.1).type,
                  ActionType::Allow);
    }
    const Action a = mod.process_message(1, 1, "msg", 100.9);
    EXPECT_EQ(a.type, ActionType::MuteTemporary);
    EXPECT_EQ(a.duration_seconds, 120);
    EXPECT_FALSE(a.delete_message);
}

}  // namespace
