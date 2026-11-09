// SPDX-License-Identifier: AGPL-3.0-only
//
// mask_sensitive_text() のテスト。鍵、招待コード、グローバルな IP アドレス、ユーザー名を伏せ、
// 状態の名前やバージョン、LAN のアドレスのように調査に要る部分は残すことを確かめる。

#include "secret_masker.h"

#include <gtest/gtest.h>

namespace vocroz::persistence {
namespace {

TEST(SecretMaskerTest, LeavesOrdinaryTextUntouched) {
    const std::string text = "接続状態が NO_SIGNAL から CONNECTING に遷移 (v0.1.0, 48000 Hz)";
    EXPECT_EQ(mask_sensitive_text(text), text);
}

TEST(SecretMaskerTest, MasksLongHexAndBase64UrlRuns) {
    EXPECT_EQ(mask_sensitive_text(
                  "key=0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef end"),
              "key=<secret> end");
    EXPECT_EQ(mask_sensitive_text("token Zm9vYmFyYmF6cXV4LWJhc2U2NHVybF9zYWZlXzI1Ng"),
              "token <secret>");
}

TEST(SecretMaskerTest, KeepsLongRunsWithoutDigits) {
    const std::string text = "SESSION_STATE_MACHINE_TRANSITION_TABLE";
    EXPECT_EQ(mask_sensitive_text(text), text);
}

TEST(SecretMaskerTest, MasksInviteCodesInBothForms) {
    EXPECT_EQ(mask_sensitive_text("code ABCDE12345 ok"), "code <invite-code> ok");
    EXPECT_EQ(mask_sensitive_text("https://vocroz.example/join#ABCDE-12345"),
              "https://vocroz.example/join#<invite-code>");
    EXPECT_EQ(mask_sensitive_text("vocroz://join?code=7Z9QK-M3XRT"),
              "vocroz://join?code=<invite-code>");
}

TEST(SecretMaskerTest, DoesNotMistakeUppercaseWordsForInviteCodes) {
    // I、L、O、U を含む語は Crockford Base32 の正規の形にならない。
    EXPECT_EQ(mask_sensitive_text("CONNECTING"), "CONNECTING");
    EXPECT_EQ(mask_sensitive_text("RECONNECTS"), "RECONNECTS");
    EXPECT_EQ(mask_sensitive_text("connection"), "connection");
}

TEST(SecretMaskerTest, MasksGlobalIpv4ButKeepsPrivateAddresses) {
    EXPECT_EQ(mask_sensitive_text("peer 203.0.113.7:49152"), "peer <ip>:49152");
    EXPECT_EQ(mask_sensitive_text("lan 192.168.1.20 and 10.0.0.1 and 127.0.0.1"),
              "lan 192.168.1.20 and 10.0.0.1 and 127.0.0.1");
    EXPECT_EQ(mask_sensitive_text("cgnat 100.64.0.1"), "cgnat 100.64.0.1");
}

TEST(SecretMaskerTest, LeavesVersionNumbersThatAreNotAddresses) {
    EXPECT_EQ(mask_sensitive_text("Windows 10.0.22631.1"), "Windows 10.0.22631.1");
    EXPECT_EQ(mask_sensitive_text("v1.2.3"), "v1.2.3");
}

TEST(SecretMaskerTest, MasksUserProfileFolderName) {
    EXPECT_EQ(mask_sensitive_text(R"(C:\Users\taro.yamada\AppData\Local\VOCROZ\logs)"),
              R"(C:\Users\<user>\AppData\Local\VOCROZ\logs)");
    EXPECT_EQ(mask_sensitive_text(R"(c:\users\さとう)"), R"(c:\users\<user>)");
    EXPECT_EQ(mask_sensitive_text("/Users/hanako/Library"), "/Users/<user>/Library");
}

}  // namespace
}  // namespace vocroz::persistence
