// SPDX-License-Identifier: AGPL-3.0-only
//
// SemanticVersion のパースと優先順位の単体テスト。

#include "vocroz/update/semantic_version.h"

#include <gtest/gtest.h>

namespace vocroz::update {
namespace {

SemanticVersion v(std::string_view text) {
    const auto parsed = parse_semantic_version(text);
    EXPECT_TRUE(parsed.has_value()) << text;
    return parsed.value_or(SemanticVersion{});
}

TEST(SemanticVersionTest, ParsesReleaseAndPrerelease) {
    EXPECT_EQ(v("1.2.3"), (SemanticVersion{1, 2, 3, ""}));
    EXPECT_EQ(v("0.1.0-alpha.2"), (SemanticVersion{0, 1, 0, "alpha.2"}));
}

TEST(SemanticVersionTest, RejectsAmbiguousOrMalformedText) {
    for (const std::string_view text :
         {"", "1", "1.2", "1.2.3.4", "01.2.3", "1.02.3", "1.2.3-", "1.2.3-alpha..1",
          "1.2.3-alpha.01", "1.2.3+build", " 1.2.3", "1.2.3 ", "1.2.x", "4294967296.0.0",
          "1.2.3-al_pha", "-1.2.3"}) {
        EXPECT_FALSE(parse_semantic_version(text).has_value()) << text;
    }
}

TEST(SemanticVersionTest, OrdersBySemverPrecedence) {
    // Semantic Versioning 2.0.0 の例と同じ並び。
    const std::string_view ordered[] = {"1.0.0-alpha", "1.0.0-alpha.1", "1.0.0-alpha.beta",
                                        "1.0.0-beta",  "1.0.0-beta.2",  "1.0.0-beta.11",
                                        "1.0.0-rc.1",  "1.0.0",         "1.0.1",
                                        "1.1.0",       "2.0.0"};
    for (std::size_t i = 0; i + 1 < std::size(ordered); ++i) {
        EXPECT_LT(v(ordered[i]), v(ordered[i + 1])) << ordered[i] << " < " << ordered[i + 1];
    }
}

TEST(SemanticVersionTest, ToStringRoundTrips) {
    EXPECT_EQ(to_string(v("0.1.0-alpha.2")), "0.1.0-alpha.2");
    EXPECT_EQ(to_string(v("10.20.30")), "10.20.30");
}

}  // namespace
}  // namespace vocroz::update
