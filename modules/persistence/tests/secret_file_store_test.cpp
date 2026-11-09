// SPDX-License-Identifier: AGPL-3.0-only
//
// SecretFileStore のテスト。種類ごとのファイルへの保存と読み込み、平文がディスクに残らないこと、
// 壊れたファイルや別の種類のファイルの置き換えを kCorrupted として返すことを確かめる。

#include "secret_file_store.h"

#include <array>
#include <memory>
#include <string>

#include <gtest/gtest.h>

#include "fakes/fake_data_protector.h"
#include "support/temp_directory.h"

namespace vocroz::persistence {
namespace {

using session::SecretError;
using session::SecretSlot;

constexpr std::array<std::uint8_t, 4> kKey{0x56, 0x4F, 0x43, 0x52};  // "VOCR"

class SecretFileStoreTest : public ::testing::Test {
protected:
    SecretFileStoreTest() {
        auto protector = std::make_unique<FakeDataProtector>();
        protector_ = protector.get();
        store_ = std::make_unique<SecretFileStore>(dir_.path() / "secrets", std::move(protector));
    }

    std::filesystem::path file(const char* name) const {
        return dir_.path() / "secrets" / name;
    }

    TempDirectory dir_;
    FakeDataProtector* protector_ = nullptr;
    std::unique_ptr<SecretFileStore> store_;
};

TEST_F(SecretFileStoreTest, ReportsNotFoundBeforeFirstSave) {
    const auto loaded = store_->load(SecretSlot::kHostStaticKey);
    ASSERT_FALSE(loaded);
    EXPECT_EQ(loaded.error(), SecretError::kNotFound);
}

TEST_F(SecretFileStoreTest, LoadsWhatWasSavedWithoutStoringPlaintext) {
    ASSERT_TRUE(store_->save(SecretSlot::kHostStaticKey, kKey));
    const auto loaded = store_->load(SecretSlot::kHostStaticKey);
    ASSERT_TRUE(loaded);
    EXPECT_EQ(loaded.value(), std::vector<std::uint8_t>(kKey.begin(), kKey.end()));
    EXPECT_EQ(read_text(file("host-static-key.bin")).find("VOCR"), std::string::npos);
}

TEST_F(SecretFileStoreTest, KeepsSlotsSeparate) {
    ASSERT_TRUE(store_->save(SecretSlot::kHostStaticKey, kKey));
    const auto client = store_->load(SecretSlot::kClientStaticKey);
    ASSERT_FALSE(client);
    EXPECT_EQ(client.error(), SecretError::kNotFound);
}

TEST_F(SecretFileStoreTest, RejectsFileMovedFromAnotherSlot) {
    ASSERT_TRUE(store_->save(SecretSlot::kHostStaticKey, kKey));
    std::filesystem::copy_file(file("host-static-key.bin"), file("client-static-key.bin"));
    const auto loaded = store_->load(SecretSlot::kClientStaticKey);
    ASSERT_FALSE(loaded);
    EXPECT_EQ(loaded.error(), SecretError::kCorrupted);
}

TEST_F(SecretFileStoreTest, ReportsCorruptedFile) {
    std::filesystem::create_directories(dir_.path() / "secrets");
    write_text(file("host-static-key.bin"), "garbage");
    const auto loaded = store_->load(SecretSlot::kHostStaticKey);
    ASSERT_FALSE(loaded);
    EXPECT_EQ(loaded.error(), SecretError::kCorrupted);
}

TEST_F(SecretFileStoreTest, OverwritesWithoutLeavingBackup) {
    ASSERT_TRUE(store_->save(SecretSlot::kHostStaticKey, kKey));
    constexpr std::array<std::uint8_t, 2> kNewKey{1, 2};
    ASSERT_TRUE(store_->save(SecretSlot::kHostStaticKey, kNewKey));
    EXPECT_EQ(store_->load(SecretSlot::kHostStaticKey).value(), (std::vector<std::uint8_t>{1, 2}));
    EXPECT_FALSE(std::filesystem::exists(file("host-static-key.bin.bak")));
}

TEST_F(SecretFileStoreTest, EraseRemovesValue) {
    ASSERT_TRUE(store_->save(SecretSlot::kHostStaticKey, kKey));
    ASSERT_TRUE(store_->erase(SecretSlot::kHostStaticKey));
    EXPECT_EQ(store_->load(SecretSlot::kHostStaticKey).error(), SecretError::kNotFound);
    EXPECT_TRUE(store_->erase(SecretSlot::kHostStaticKey));
}

TEST_F(SecretFileStoreTest, ReportsErrorWhenProtectionFails) {
    protector_->fail_protect();
    const auto saved = store_->save(SecretSlot::kHostStaticKey, kKey);
    ASSERT_FALSE(saved);
    EXPECT_EQ(saved.error(), SecretError::kIoError);
    EXPECT_FALSE(std::filesystem::exists(file("host-static-key.bin")));
}

}  // namespace
}  // namespace vocroz::persistence
