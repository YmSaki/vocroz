// SPDX-License-Identifier: AGPL-3.0-only
//
// load_host_keys() の単体テスト。保存した鍵の再利用と、作り直しの条件を確かめる。

#include "host_key_loader.h"

#include <vector>

#include <gtest/gtest.h>

#include "fakes/fake_logger.h"
#include "fakes/fake_secret_store.h"

namespace vocroz::engine {
namespace {

using session::SecretError;
using session::SecretSlot;

std::vector<std::uint8_t> private_bytes(const transport::StaticKeyPair& keys) {
    return {keys.private_key().begin(), keys.private_key().end()};
}

class HostKeyLoaderTest : public ::testing::Test {
protected:
    session::FakeSecretStore secrets_;
    common::FakeLogger logger_;
};

TEST_F(HostKeyLoaderTest, CreatesAndSavesKeyOnFirstStart) {
    auto keys = load_host_keys(secrets_, logger_);
    ASSERT_TRUE(keys);
    EXPECT_EQ(secrets_.stored(SecretSlot::kHostStaticKey), private_bytes(keys.value()));
}

TEST_F(HostKeyLoaderTest, ReusesSavedKeyAcrossStarts) {
    auto first = load_host_keys(secrets_, logger_);
    ASSERT_TRUE(first);
    auto second = load_host_keys(secrets_, logger_);
    ASSERT_TRUE(second);
    // 再起動しても公開鍵が変わらない（招待と再接続で同じ親機と確かめられる）。
    EXPECT_EQ(second.value().public_key(), first.value().public_key());
    EXPECT_EQ(secrets_.save_count(), 1);
}

TEST_F(HostKeyLoaderTest, RecreatesKeyWhenSavedValueIsCorrupted) {
    secrets_.fail_load(SecretError::kCorrupted);
    auto keys = load_host_keys(secrets_, logger_);
    ASSERT_TRUE(keys);
    EXPECT_EQ(secrets_.save_count(), 1);
}

TEST_F(HostKeyLoaderTest, RecreatesKeyWhenSavedValueHasWrongLength) {
    secrets_.put(SecretSlot::kHostStaticKey, std::vector<std::uint8_t>(5, 1));
    auto keys = load_host_keys(secrets_, logger_);
    ASSERT_TRUE(keys);
    EXPECT_EQ(secrets_.stored(SecretSlot::kHostStaticKey), private_bytes(keys.value()));
}

TEST_F(HostKeyLoaderTest, DoesNotOverwriteSavedKeyWhenLoadFailsWithIoError) {
    const std::vector<std::uint8_t> saved(transport::kPrivateKeyBytes, 9);
    secrets_.put(SecretSlot::kHostStaticKey, saved);
    secrets_.fail_load(SecretError::kIoError);

    auto keys = load_host_keys(secrets_, logger_);

    ASSERT_TRUE(keys);
    EXPECT_EQ(secrets_.save_count(), 0);
    EXPECT_EQ(secrets_.stored(SecretSlot::kHostStaticKey), saved);
}

TEST_F(HostKeyLoaderTest, KeepsRunningWhenSaveFails) {
    secrets_.fail_save(SecretError::kIoError);
    auto keys = load_host_keys(secrets_, logger_);
    EXPECT_TRUE(keys);
}

}  // namespace
}  // namespace vocroz::engine
