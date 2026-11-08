// SPDX-License-Identifier: AGPL-3.0-only
//
// 埋め込んだ UI のファイルの検索と MIME タイプの単体テスト。

#include "vocroz/webui/ui_resource.h"

#include <array>
#include <string>
#include <string_view>

#include <gtest/gtest.h>

namespace vocroz::webui {
namespace {

constexpr unsigned char kIndex[] = {'<', 'h', 't', 'm', 'l', '>'};
constexpr unsigned char kScript[] = {'j', 's'};
constexpr unsigned char kDotted[] = {'d'};

constexpr std::array<UiAsset, 3> kAssets{{
    {"index.html", kIndex},
    {"assets/index-abc123.js", kScript},
    {"assets/a..b.css", kDotted},
}};

std::string text_of(const UiResource& resource) {
    return {reinterpret_cast<const char*>(resource.data.data()), resource.data.size()};
}

TEST(UiResourceTest, RootServesIndexHtml) {
    const auto found = find_ui_resource(kAssets, "/");
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(text_of(*found), "<html>");
    EXPECT_EQ(found->mime_type, "text/html; charset=utf-8");
}

TEST(UiResourceTest, EmptyPathServesIndexHtml) {
    EXPECT_TRUE(find_ui_resource(kAssets, "").has_value());
}

TEST(UiResourceTest, FindsNestedAssetAndIgnoresQueryAndFragment) {
    for (const std::string_view path :
         {"/assets/index-abc123.js", "/assets/index-abc123.js?v=1", "/assets/index-abc123.js#x"}) {
        const auto found = find_ui_resource(kAssets, path);
        ASSERT_TRUE(found.has_value()) << path;
        EXPECT_EQ(text_of(*found), "js");
        EXPECT_EQ(found->mime_type, "text/javascript; charset=utf-8");
    }
}

TEST(UiResourceTest, ReturnsNothingForUnknownPath) {
    EXPECT_FALSE(find_ui_resource(kAssets, "/missing.js").has_value());
    EXPECT_FALSE(find_ui_resource(kAssets, "/assets/").has_value());
}

TEST(UiResourceTest, RejectsParentSegmentsAndBackslashes) {
    EXPECT_FALSE(find_ui_resource(kAssets, "/assets/../index.html").has_value());
    EXPECT_FALSE(find_ui_resource(kAssets, "/../index.html").has_value());
    EXPECT_FALSE(find_ui_resource(kAssets, "/assets\\index-abc123.js").has_value());
}

TEST(UiResourceTest, AllowsDotsInsideFileNames) {
    EXPECT_TRUE(find_ui_resource(kAssets, "/assets/a..b.css").has_value());
}

TEST(UiResourceTest, ChoosesMimeTypeByExtension) {
    EXPECT_EQ(mime_type_for("a.css"), "text/css; charset=utf-8");
    EXPECT_EQ(mime_type_for("a.SVG"), "image/svg+xml");
    EXPECT_EQ(mime_type_for("font.woff2"), "font/woff2");
    EXPECT_EQ(mime_type_for("index.js.map"), "application/json; charset=utf-8");
    EXPECT_EQ(mime_type_for("data.bin"), "application/octet-stream");
    EXPECT_EQ(mime_type_for("noextension"), "application/octet-stream");
}

}  // namespace
}  // namespace vocroz::webui
