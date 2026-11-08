// SPDX-License-Identifier: AGPL-3.0-only
//
// AtomicFile のテスト。一時ファイル → 置き換えの順序と、1 世代のバックアップの残し方を確かめる。

#include "atomic_file.h"

#include <string>

#include <gtest/gtest.h>

#include "support/temp_directory.h"

namespace vocroz::persistence {
namespace {

class AtomicFileTest : public ::testing::Test {
protected:
    std::filesystem::path main_path() const {
        return dir_.path() / "a.json";
    }

    TempDirectory dir_;
};

TEST_F(AtomicFileTest, ReadsNothingWhenFileDoesNotExist) {
    const AtomicFile file{main_path()};
    const auto current = file.read_current();
    ASSERT_TRUE(current);
    EXPECT_FALSE(current.value().has_value());
    const auto backup = file.read_backup();
    ASSERT_TRUE(backup);
    EXPECT_FALSE(backup.value().has_value());
}

TEST_F(AtomicFileTest, CommitWritesContentAndRemovesTemporaryFile) {
    AtomicFile file{main_path()};
    ASSERT_TRUE(file.commit(std::string{"one"}, true));
    EXPECT_EQ(read_text(main_path()), "one");
    EXPECT_FALSE(std::filesystem::exists(dir_.path() / "a.json.tmp"));
    EXPECT_FALSE(std::filesystem::exists(dir_.path() / "a.json.bak"));
}

TEST_F(AtomicFileTest, CommitKeepsPreviousContentAsBackup) {
    AtomicFile file{main_path()};
    ASSERT_TRUE(file.commit(std::string{"one"}, true));
    ASSERT_TRUE(file.commit(std::string{"two"}, true));
    EXPECT_EQ(*file.read_current().value(), "two");
    EXPECT_EQ(*file.read_backup().value(), "one");
}

TEST_F(AtomicFileTest, CommitWithoutBackupLeavesExistingBackupUntouched) {
    AtomicFile file{main_path()};
    ASSERT_TRUE(file.commit(std::string{"good"}, true));
    ASSERT_TRUE(file.commit(std::string{"broken"}, true));
    // 本体が壊れているときは呼び出し側がバックアップに回さない。正常なバックアップは残る。
    ASSERT_TRUE(file.commit(std::string{"three"}, false));
    EXPECT_EQ(*file.read_current().value(), "three");
    EXPECT_EQ(*file.read_backup().value(), "good");
}

TEST_F(AtomicFileTest, CreatesParentDirectoryOnCommit) {
    AtomicFile file{dir_.path() / "nested" / "b.bin"};
    ASSERT_TRUE(file.commit(std::string{"x"}, false));
    EXPECT_EQ(read_text(dir_.path() / "nested" / "b.bin"), "x");
}

TEST_F(AtomicFileTest, LeftoverTemporaryFileIsIgnoredAndOverwritten) {
    // 前回の保存が一時ファイルを書いている途中で止まった状態。
    write_text(main_path(), "old");
    write_text(dir_.path() / "a.json.tmp", "half-writ");
    AtomicFile file{main_path()};
    EXPECT_EQ(*file.read_current().value(), "old");
    ASSERT_TRUE(file.commit(std::string{"new"}, true));
    EXPECT_EQ(*file.read_current().value(), "new");
    EXPECT_EQ(*file.read_backup().value(), "old");
}

TEST_F(AtomicFileTest, RejectsFilesLargerThanLimit) {
    write_text(main_path(), std::string(1024 * 1024 + 1, 'x'));
    const AtomicFile file{main_path()};
    const auto current = file.read_current();
    ASSERT_FALSE(current);
    EXPECT_EQ(current.error(), AtomicFileError::kTooLarge);
}

TEST_F(AtomicFileTest, RemoveAllDeletesMainTemporaryAndBackup) {
    AtomicFile file{main_path()};
    ASSERT_TRUE(file.commit(std::string{"one"}, true));
    ASSERT_TRUE(file.commit(std::string{"two"}, true));
    write_text(dir_.path() / "a.json.tmp", "t");
    ASSERT_TRUE(file.remove_all());
    EXPECT_FALSE(std::filesystem::exists(main_path()));
    EXPECT_FALSE(std::filesystem::exists(dir_.path() / "a.json.tmp"));
    EXPECT_FALSE(std::filesystem::exists(dir_.path() / "a.json.bak"));
    // 何もない状態で消しても成功する。
    EXPECT_TRUE(file.remove_all());
}

}  // namespace
}  // namespace vocroz::persistence
