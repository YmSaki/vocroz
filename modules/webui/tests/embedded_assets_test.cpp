// SPDX-License-Identifier: AGPL-3.0-only
//
// vocroz_embed_ui_assets()（cmake/EmbedUiAssets.cmake）の単体テスト。
// 見本のフォルダ（tests/data/ui-sample）を埋め込み、一覧と内容がファイルと一致することを確かめる。

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "vocroz/webui/ui_assets.h"
#include "vocroz/webui/ui_resource.h"

namespace vocroz::webui::test {
UiAssetTable sample_ui_assets();
}  // namespace vocroz::webui::test

namespace vocroz::webui {
namespace {

std::string read_file(const std::filesystem::path& path) {
    std::ifstream stream{path, std::ios::binary};
    return {std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
}

TEST(EmbeddedAssetsTest, ListsEveryFileWithSlashSeparatedPaths) {
    std::vector<std::string> paths;
    for (const auto& asset : test::sample_ui_assets()) {
        paths.emplace_back(asset.path);
    }
    std::sort(paths.begin(), paths.end());
    EXPECT_EQ(paths, (std::vector<std::string>{"assets/app.js", "assets/empty.txt", "index.html"}));
}

TEST(EmbeddedAssetsTest, ContentsMatchTheFilesByteForByte) {
    const std::filesystem::path root{VOCROZ_WEBUI_SAMPLE_DIR};
    for (const auto& asset : test::sample_ui_assets()) {
        const std::string expected = read_file(root / std::filesystem::path{asset.path});
        const std::string actual{reinterpret_cast<const char*>(asset.data.data()),
                                 asset.data.size()};
        EXPECT_EQ(actual, expected) << asset.path;
    }
}

TEST(EmbeddedAssetsTest, ServesEmbeddedIndexForRoot) {
    const auto found = find_ui_resource(test::sample_ui_assets(), "/");
    ASSERT_TRUE(found.has_value());
    const std::string text{reinterpret_cast<const char*>(found->data.data()), found->data.size()};
    EXPECT_NE(text.find("見本"), std::string::npos);
}

}  // namespace
}  // namespace vocroz::webui
