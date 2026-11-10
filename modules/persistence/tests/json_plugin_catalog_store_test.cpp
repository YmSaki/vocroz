// SPDX-License-Identifier: AGPL-3.0-only
//
// JsonPluginCatalogStore のテスト。保存と読み込み、記録がないときと壊れているときの扱いを確かめる。

#include "json_plugin_catalog_store.h"

#include <vector>

#include <gtest/gtest.h>

#include "fakes/fake_logger.h"
#include "support/temp_directory.h"

namespace vocroz::persistence {
namespace {

using session::SettingsError;

class JsonPluginCatalogStoreTest : public ::testing::Test {
protected:
    TempDirectory dir_;
    common::FakeLogger logger_;
    JsonPluginCatalogStore store_{dir_.path(), logger_};
};

TEST_F(JsonPluginCatalogStoreTest, ReportsNotFoundBeforeTheFirstSave) {
    const auto loaded = store_.load();
    ASSERT_FALSE(loaded);
    EXPECT_EQ(loaded.error(), SettingsError::kNotFound);
}

TEST_F(JsonPluginCatalogStoreTest, LoadsWhatWasSaved) {
    session::PluginScanRecord record;
    record.file = session::PluginFile{"C:/VST3/EQ.vst3", 42, 1000};
    session::PluginDescription eq;
    eq.name = "EQ";
    eq.identifier = "VST3-EQ-1";
    record.plugins.push_back(eq);
    const std::vector<session::PluginScanRecord> records{record};
    ASSERT_TRUE(store_.save(records));

    JsonPluginCatalogStore reopened{dir_.path(), logger_};
    const auto loaded = reopened.load();
    ASSERT_TRUE(loaded);
    EXPECT_EQ(loaded.value(), records);
}

TEST_F(JsonPluginCatalogStoreTest, ReportsACorruptedFileSoThatEverythingIsRescanned) {
    write_text(dir_.path() / "plugin-catalog.json", "garbage");
    const auto loaded = store_.load();
    ASSERT_FALSE(loaded);
    EXPECT_EQ(loaded.error(), SettingsError::kCorrupted);
    EXPECT_FALSE(logger_.entries().empty());
}

}  // namespace
}  // namespace vocroz::persistence
