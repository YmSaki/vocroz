// SPDX-License-Identifier: AGPL-3.0-only
//
// nlohmann/json のリンク確認テスト。パースとシリアライズの往復を確かめる。

#include <string>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

namespace {

TEST(NlohmannJsonLinkTest, RoundTripsObject) {
    const std::string text = R"({"entity_id":"ch-1","expected_revision":3})";
    const nlohmann::json parsed = nlohmann::json::parse(text);
    EXPECT_EQ(parsed.at("entity_id").get<std::string>(), "ch-1");
    EXPECT_EQ(parsed.at("expected_revision").get<int>(), 3);
    EXPECT_EQ(nlohmann::json::parse(parsed.dump()), parsed);
}

TEST(NlohmannJsonLinkTest, ReportsInvalidInputWithoutThrowing) {
    // 信頼境界のパーサーは例外を外に出さないので、例外を使わない呼び出し方が動くことを確かめる。
    const nlohmann::json parsed = nlohmann::json::parse("{", nullptr, /*allow_exceptions=*/false);
    EXPECT_TRUE(parsed.is_discarded());
}

}  // namespace
