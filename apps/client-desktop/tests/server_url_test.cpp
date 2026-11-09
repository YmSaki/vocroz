// SPDX-License-Identifier: AGPL-3.0-only
//
// ランデブーサーバーのベース URL からホスト名を取り出す処理のテスト。

#include "server_url.h"

#include <gtest/gtest.h>

namespace vocroz::client {
namespace {

TEST(ServerUrlTest, ExtractsHostFromHttpsUrl) {
    EXPECT_EQ(rendezvous_host_from_url("https://rendezvous.example"), "rendezvous.example");
}

TEST(ServerUrlTest, IgnoresPortAndPath) {
    EXPECT_EQ(rendezvous_host_from_url("https://rendezvous.example:8443/vocroz"),
              "rendezvous.example");
    EXPECT_EQ(rendezvous_host_from_url("http://localhost:8080"), "localhost");
}

TEST(ServerUrlTest, StripsBracketsFromIpv6Literal) {
    EXPECT_EQ(rendezvous_host_from_url("http://[::1]:8080"), "::1");
}

TEST(ServerUrlTest, RejectsMalformedUrls) {
    EXPECT_EQ(rendezvous_host_from_url(""), std::nullopt);
    EXPECT_EQ(rendezvous_host_from_url("rendezvous.example"), std::nullopt);
    EXPECT_EQ(rendezvous_host_from_url("ftp://rendezvous.example"), std::nullopt);
    EXPECT_EQ(rendezvous_host_from_url("https://"), std::nullopt);
    EXPECT_EQ(rendezvous_host_from_url("https://user@evil.example"), std::nullopt);
    EXPECT_EQ(rendezvous_host_from_url("https://[]"), std::nullopt);
}

}  // namespace
}  // namespace vocroz::client
