// SPDX-License-Identifier: AGPL-3.0-only
//
// FilesystemMediaLibrary の単体テスト。メディアフォルダの音声ファイルだけを、フォルダからの
// 相対パスで、決まった順に、上限の数まで返すことを確かめる。

#include "filesystem_media_library.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "support/temp_directory.h"
#include "utf8_path.h"

namespace vocroz::engine {
namespace {

void touch(const std::filesystem::path& file) {
    std::filesystem::create_directories(file.parent_path());
    std::ofstream(file) << "x";
}

TEST(FilesystemMediaLibraryTest, ListsAudioFilesSortedWithRelativePaths) {
    TempDirectory temp;
    touch(temp.path() / "b.wav");
    touch(temp.path() / "a.FLAC");
    touch(temp.path() / "take" / "01-Master.wav");
    touch(temp.path() / "notes.txt");
    FilesystemMediaLibrary library;
    const auto files = library.list(path_utf8(temp.path()), 100);
    EXPECT_EQ(files, (std::vector<std::string>{"a.FLAC", "b.wav", "take/01-Master.wav"}));
}

TEST(FilesystemMediaLibraryTest, SearchesTwoSubfolderLevels) {
    TempDirectory temp;
    touch(temp.path() / "1" / "2" / "3" / "deep.wav");
    touch(temp.path() / "1" / "2" / "two.wav");
    touch(temp.path() / "1" / "one.wav");
    FilesystemMediaLibrary library;
    EXPECT_EQ(library.list(path_utf8(temp.path()), 100),
              (std::vector<std::string>{"1/2/two.wav", "1/one.wav"}));
}

TEST(FilesystemMediaLibraryTest, StopsAtLimit) {
    TempDirectory temp;
    touch(temp.path() / "a.wav");
    touch(temp.path() / "b.wav");
    touch(temp.path() / "c.wav");
    FilesystemMediaLibrary library;
    EXPECT_EQ(library.list(path_utf8(temp.path()), 2),
              (std::vector<std::string>{"a.wav", "b.wav"}));
}

TEST(FilesystemMediaLibraryTest, MissingFolderIsEmpty) {
    TempDirectory temp;
    FilesystemMediaLibrary library;
    EXPECT_TRUE(library.list(path_utf8(temp.path() / "none"), 100).empty());
}

TEST(FilesystemMediaLibraryTest, KeepsNonAsciiNames) {
    TempDirectory temp;
    touch(temp.path() / utf8_path("伴奏.wav"));
    FilesystemMediaLibrary library;
    const auto files = library.list(path_utf8(temp.path()), 100);
    ASSERT_EQ(files.size(), 1U);
    EXPECT_EQ(files[0], "伴奏.wav");
    EXPECT_EQ(utf8_path(library.resolve(path_utf8(temp.path()), files[0])),
              temp.path() / utf8_path("伴奏.wav"));
}

}  // namespace
}  // namespace vocroz::engine
