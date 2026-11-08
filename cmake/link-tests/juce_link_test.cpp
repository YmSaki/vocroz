// SPDX-License-Identifier: AGPL-3.0-only
//
// JUCE のリンク確認テスト。juce_core の型を生成して使えることを確かめる。

#include <gtest/gtest.h>
#include <juce_core/juce_core.h>

namespace {

TEST(JuceLinkTest, UsesJuceCoreString) {
    const juce::String text("VOCROZ");
    EXPECT_EQ(text.length(), 6);
    EXPECT_TRUE(text.startsWith("VOC"));
}

}  // namespace
