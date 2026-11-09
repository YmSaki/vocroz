// SPDX-License-Identifier: AGPL-3.0-only
//
// find_plugin_files() のテスト。一時ディレクトリにファイルとバンドルを作り、
// 列挙の対象、バンドルの扱い、更新の検出を検証する。

#include "plugin_file_finder.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace vocroz::plugin_host {
namespace {

namespace fs = std::filesystem;

void write_file(const fs::path& path, std::size_t bytes) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out << std::string(bytes, 'x');
}

std::string utf8(const fs::path& path) {
    const std::u8string text = path.u8string();
    return {text.begin(), text.end()};
}

class PluginFileFinderTest : public ::testing::Test {
protected:
    void SetUp() override {
        std::random_device random;
        root_ = fs::temp_directory_path() / ("vocroz-finder-" + std::to_string(random()));
        fs::create_directories(root_);
    }
    void TearDown() override {
        std::error_code error;
        fs::remove_all(root_, error);
    }

    std::vector<session::PluginFile> find() const {
        const std::vector<fs::path> directories{root_};
        return find_plugin_files(directories);
    }

    fs::path root_;
};

TEST_F(PluginFileFinderTest, ListsVst3FilesAndBundlesButNotOtherFiles) {
    write_file(root_ / "Single.vst3", 10);
    write_file(root_ / "Vendor" / "Upper.VST3", 20);
    write_file(root_ / "Bundle.vst3" / "Contents" / "x86_64-win" / "Bundle.vst3", 30);
    write_file(root_ / "Bundle.vst3" / "Contents" / "Resources" / "moduleinfo.json", 5);
    write_file(root_ / "readme.txt", 1);

    const auto files = find();
    ASSERT_EQ(files.size(), 3U);
    // バンドルは 1 つのファイルとして扱い、中の .vst3 は別に数えない。
    EXPECT_EQ(files[0].path, utf8(root_ / "Bundle.vst3"));
    EXPECT_EQ(files[0].size_bytes, 35U);
    EXPECT_EQ(files[1].path, utf8(root_ / "Single.vst3"));
    EXPECT_EQ(files[1].size_bytes, 10U);
    EXPECT_EQ(files[2].path, utf8(root_ / "Vendor" / "Upper.VST3"));
}

TEST_F(PluginFileFinderTest, DetectsAnUpdateInsideABundle) {
    const fs::path binary = root_ / "Bundle.vst3" / "Contents" / "x86_64-win" / "Bundle.vst3";
    write_file(binary, 30);
    const auto before = find();

    // バンドルのディレクトリはそのままで、中のバイナリだけが新しくなる更新。
    fs::last_write_time(binary, fs::last_write_time(binary) + std::chrono::hours(1));
    const auto after = find();
    ASSERT_EQ(before.size(), 1U);
    ASSERT_EQ(after.size(), 1U);
    EXPECT_NE(before[0], after[0]);
}

TEST_F(PluginFileFinderTest, SkipsMissingDirectoriesAndDeduplicatesOverlaps) {
    write_file(root_ / "A.vst3", 1);
    const std::vector<fs::path> directories{root_ / "missing", root_, root_};
    const auto files = find_plugin_files(directories);
    ASSERT_EQ(files.size(), 1U);
    EXPECT_EQ(files[0].path, utf8(root_ / "A.vst3"));
}

}  // namespace
}  // namespace vocroz::plugin_host
