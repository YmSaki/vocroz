// SPDX-License-Identifier: AGPL-3.0-only
//
// ダイアログの文言のテスト。言語の決め方と、どの言語にも文言があることを確かめる。

#include "dialog_text.h"

#include <gtest/gtest.h>

namespace vocroz::pa_desktop {
namespace {

TEST(DialogTextTest, JapaneseOnlyForJa) {
    EXPECT_EQ(dialog_language_from_tag("ja"), DialogLanguage::kJapanese);
    EXPECT_EQ(dialog_language_from_tag("en"), DialogLanguage::kEnglish);
    EXPECT_EQ(dialog_language_from_tag(""), DialogLanguage::kEnglish);
}

TEST(DialogTextTest, EveryLanguageHasEveryText) {
    for (const auto language : {DialogLanguage::kJapanese, DialogLanguage::kEnglish}) {
        EXPECT_FALSE(host_close_text(language).message.empty());
        EXPECT_FALSE(client_close_text(language).message.empty());
        EXPECT_FALSE(engine_unavailable_text(language).message.empty());
        EXPECT_FALSE(startup_failed_text(language).message.empty());
        EXPECT_FALSE(routing_window_title(language).empty());
    }
    EXPECT_NE(host_close_text(DialogLanguage::kJapanese).message,
              host_close_text(DialogLanguage::kEnglish).message);
}

}  // namespace
}  // namespace vocroz::pa_desktop
