// SPDX-License-Identifier: AGPL-3.0-only
//
// miniupnpc のリンク確認テスト。ネットワークに触れない関数（エラーコードの文字列化）だけを呼ぶ。
// ルーターとのやり取りは CI で再現できないので、使い方は実機で確かめる。

#include <string_view>

#include <gtest/gtest.h>
#include <upnperrors.h>

namespace {

TEST(MiniupnpcLinkTest, DescribesUpnpErrorCodes) {
    const char* description = strupnperror(718);
    ASSERT_NE(description, nullptr);
    EXPECT_EQ(std::string_view(description), "ConflictInMappingEntry");
}

}  // namespace
