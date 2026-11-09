// SPDX-License-Identifier: AGPL-3.0-only
//
// MemorySettingsStore の単体テスト。

#include "memory_settings_store.h"

#include <gtest/gtest.h>

namespace vocroz::engine {
namespace {

// 起動のたびに初回の起動として振る舞う（設定ファイルが入るまでの仮の実装）。
TEST(MemorySettingsStoreTest, ReportsNotFoundBeforeSaving) {
    MemorySettingsStore store;
    ASSERT_FALSE(store.load_host().has_value());
    EXPECT_EQ(store.load_host().error(), session::SettingsError::kNotFound);
    EXPECT_EQ(store.load_client().error(), session::SettingsError::kNotFound);
}

TEST(MemorySettingsStoreTest, ReturnsWhatWasSaved) {
    MemorySettingsStore store;
    session::HostSettings settings;
    settings.session_name = "リハーサル";
    settings.output_device_id = "{0.0.0.00000000}.{abc}";
    ASSERT_TRUE(store.save_host(settings).has_value());
    ASSERT_TRUE(store.load_host().has_value());
    EXPECT_EQ(store.load_host().value(), settings);
}

}  // namespace
}  // namespace vocroz::engine
