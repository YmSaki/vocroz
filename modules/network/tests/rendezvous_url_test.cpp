// SPDX-License-Identifier: AGPL-3.0-only
//
// ランデブーサーバーの URL の検証のテスト。

#include "vocroz/network/rendezvous_url.h"

#include <gtest/gtest.h>

namespace vocroz::network {
namespace {

TEST(RendezvousUrlTest, SplitsAnHttpsUrlWithTheDefaultPort) {
    const auto url = parse_rendezvous_url("https://vocroz.example");
    ASSERT_TRUE(url.has_value());
    EXPECT_EQ(url.value(), (RendezvousUrl{true, "vocroz.example", 443, ""}));
}

TEST(RendezvousUrlTest, KeepsAnExplicitPortAndABasePathWithoutTheTrailingSlash) {
    const auto url = parse_rendezvous_url("HTTPS://vocroz.example:8443/rendezvous/");
    ASSERT_TRUE(url.has_value());
    EXPECT_EQ(url.value(), (RendezvousUrl{true, "vocroz.example", 8443, "/rendezvous"}));
}

TEST(RendezvousUrlTest, AcceptsABracketedIpv6Literal) {
    const auto url = parse_rendezvous_url("https://[2001:db8::1]:443/");
    ASSERT_TRUE(url.has_value());
    EXPECT_EQ(url.value(), (RendezvousUrl{true, "2001:db8::1", 443, ""}));
}

TEST(RendezvousUrlTest, AcceptsPlainHttpOnlyForLoopbackHosts) {
    EXPECT_EQ(parse_rendezvous_url("http://127.0.0.1:8080").value(),
              (RendezvousUrl{false, "127.0.0.1", 8080, ""}));
    EXPECT_TRUE(parse_rendezvous_url("http://localhost").has_value());
    EXPECT_TRUE(parse_rendezvous_url("http://[::1]:8080").has_value());
    EXPECT_FALSE(parse_rendezvous_url("http://vocroz.example").has_value());
    EXPECT_FALSE(parse_rendezvous_url("http://192.168.1.10:8080").has_value());
}

TEST(RendezvousUrlTest, RejectsMalformedUrls) {
    for (const char* text :
         {"", "vocroz.example", "ftp://vocroz.example", "https://", "https://:443",
          "https://vocroz.example:", "https://vocroz.example:0", "https://vocroz.example:70000",
          "https://vocroz.example:12a", "https://user@vocroz.example",
          "https://vocroz.example/?x=1", "https://vocroz.example/#frag", "https://[::1",
          "https://[]", "https://voc roz.example", "https://[::1]x", "https://vocroz.example/a b",
          "https://vocroz.example/\xE3\x81\x82"}) {
        EXPECT_FALSE(parse_rendezvous_url(text).has_value()) << text;
    }
}

}  // namespace
}  // namespace vocroz::network
