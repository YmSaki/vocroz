// SPDX-License-Identifier: AGPL-3.0-only
//
// PluginCatalog のテスト。
// 変更のないファイルを再スキャンしないこと、落ちたり止まったりしたファイルを以降のスキャンから外すこと、
// ヘルパーを起動できないときに記録を壊さないことを検証する。

#include "vocroz/session/plugin_catalog.h"

#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "fakes/fake_plugin_catalog_store.h"
#include "fakes/fake_plugin_scanner.h"

namespace vocroz::session {
namespace {

PluginFile file(const std::string& path, std::int64_t modified = 1, std::uint64_t size = 100) {
    return PluginFile{path, modified, size};
}

PluginDescription plugin(const std::string& name, const std::string& path) {
    PluginDescription description;
    description.name = name;
    description.file_path = path;
    description.identifier = name + "-id";
    description.input_channels = 2;
    description.output_channels = 2;
    return description;
}

std::vector<std::string> names(const std::vector<PluginDescription>& plugins) {
    std::vector<std::string> result;
    for (const PluginDescription& p : plugins) {
        result.push_back(p.name);
    }
    return result;
}

// begin_rescan() のあと、残りがなくなるまで scan_next() を呼ぶ。
PluginScanProgress scan_all(PluginCatalog& catalog) {
    catalog.begin_rescan();
    PluginScanProgress progress = PluginScanProgress::kDone;
    do {
        progress = catalog.scan_next();
    } while (progress == PluginScanProgress::kMore);
    return progress;
}

class PluginCatalogTest : public ::testing::Test {
protected:
    FakePluginScanner scanner_;
    FakePluginCatalogStore store_;
    PluginCatalog catalog_{scanner_, store_};
};

TEST_F(PluginCatalogTest, ScansEveryFileOnTheFirstRunAndSavesTheRecords) {
    scanner_.set_files({file("/b.vst3"), file("/a.vst3")});
    scanner_.set_plugins("/a.vst3", {plugin("Reverb", "/a.vst3")});
    scanner_.set_plugins("/b.vst3", {plugin("Comp", "/b.vst3"), plugin("Gate", "/b.vst3")});
    catalog_.load();

    EXPECT_EQ(catalog_.begin_rescan(), 2U);
    EXPECT_TRUE(catalog_.scanning());
    EXPECT_EQ(catalog_.scan_next(), PluginScanProgress::kMore);
    EXPECT_EQ(catalog_.scan_next(), PluginScanProgress::kDone);
    EXPECT_FALSE(catalog_.scanning());

    // パスの順に並ぶ。
    EXPECT_EQ(names(catalog_.available_plugins()),
              (std::vector<std::string>{"Reverb", "Comp", "Gate"}));
    ASSERT_TRUE(store_.records().has_value());
    EXPECT_EQ(*store_.records(), catalog_.records());
}

TEST_F(PluginCatalogTest, DoesNotRescanFilesThatHaveNotChanged) {
    scanner_.set_files({file("/a.vst3", 1, 100)});
    scanner_.set_plugins("/a.vst3", {plugin("Reverb", "/a.vst3")});
    catalog_.load();
    scan_all(catalog_);

    // 次の起動。保存した記録から始める。
    PluginCatalog next{scanner_, store_};
    next.load();
    EXPECT_EQ(next.begin_rescan(), 0U);
    EXPECT_EQ(scanner_.scanned().size(), 1U);
    EXPECT_EQ(names(next.available_plugins()), (std::vector<std::string>{"Reverb"}));
}

TEST_F(PluginCatalogTest, RescansAFileWhoseModifiedTimeOrSizeChanged) {
    scanner_.set_files({file("/a.vst3", 1, 100), file("/b.vst3", 1, 100)});
    catalog_.load();
    scan_all(catalog_);

    // 日時を保ったままのコピーで置き換えたファイルも、サイズの違いで見つける。
    scanner_.set_files({file("/a.vst3", 2, 100), file("/b.vst3", 1, 200)});
    EXPECT_EQ(catalog_.begin_rescan(), 2U);
}

TEST_F(PluginCatalogTest, ExcludesAFileThatCrashedOrHungFromLaterScans) {
    scanner_.set_files({file("/crash.vst3"), file("/hang.vst3"), file("/ok.vst3")});
    scanner_.fail("/crash.vst3", PluginScanFailure::kCrashed);
    scanner_.fail("/hang.vst3", PluginScanFailure::kTimedOut);
    scanner_.set_plugins("/ok.vst3", {plugin("Eq", "/ok.vst3")});
    catalog_.load();
    EXPECT_EQ(scan_all(catalog_), PluginScanProgress::kDone);

    EXPECT_EQ(names(catalog_.available_plugins()), (std::vector<std::string>{"Eq"}));
    ASSERT_EQ(catalog_.records().size(), 3U);
    EXPECT_EQ(catalog_.records()[0].status, PluginScanStatus::kCrashed);
    EXPECT_EQ(catalog_.records()[1].status, PluginScanStatus::kTimedOut);

    // 次の起動でも、変更のない不良ファイルはスキャンしない（ヘルパーを無駄に待たない）。
    PluginCatalog next{scanner_, store_};
    next.load();
    EXPECT_EQ(next.begin_rescan(), 0U);
    EXPECT_EQ(names(next.available_plugins()), (std::vector<std::string>{"Eq"}));
}

TEST_F(PluginCatalogTest, RetriesABlockedFileAfterItIsUpdated) {
    scanner_.set_files({file("/a.vst3", 1)});
    scanner_.fail("/a.vst3", PluginScanFailure::kCrashed);
    catalog_.load();
    scan_all(catalog_);
    EXPECT_TRUE(catalog_.available_plugins().empty());

    // 修正版に入れ替えられたら、もう一度スキャンする。
    scanner_.set_files({file("/a.vst3", 2)});
    scanner_.set_plugins("/a.vst3", {plugin("Fixed", "/a.vst3")});
    scan_all(catalog_);
    EXPECT_EQ(names(catalog_.available_plugins()), (std::vector<std::string>{"Fixed"}));
}

TEST_F(PluginCatalogTest, DropsRecordsOfRemovedFiles) {
    scanner_.set_files({file("/a.vst3"), file("/b.vst3")});
    scanner_.set_plugins("/a.vst3", {plugin("A", "/a.vst3")});
    scanner_.set_plugins("/b.vst3", {plugin("B", "/b.vst3")});
    catalog_.load();
    scan_all(catalog_);

    scanner_.set_files({file("/b.vst3")});
    EXPECT_EQ(catalog_.begin_rescan(), 0U);
    EXPECT_EQ(names(catalog_.available_plugins()), (std::vector<std::string>{"B"}));
    // スキャンするものがなくても、消した結果を保存する。
    ASSERT_TRUE(store_.records().has_value());
    EXPECT_EQ(store_.records()->size(), 1U);
}

TEST_F(PluginCatalogTest, StopsWithoutRecordingWhenTheHelperCannotStart) {
    scanner_.set_files({file("/a.vst3"), file("/b.vst3")});
    scanner_.fail("/a.vst3", PluginScanFailure::kScannerUnavailable);
    catalog_.load();

    EXPECT_EQ(scan_all(catalog_), PluginScanProgress::kScannerUnavailable);
    // ヘルパーの問題でプラグインの問題ではないので、読み込み不可として記録しない。
    EXPECT_TRUE(catalog_.records().empty());
    EXPECT_FALSE(catalog_.scanning());
    EXPECT_EQ(scanner_.scanned().size(), 1U);
    EXPECT_EQ(store_.save_count(), 0);
}

TEST_F(PluginCatalogTest, UsesTheListedPathForPluginsTheHelperReported) {
    scanner_.set_files({file("/a.vst3")});
    // ヘルパーが別の表記のパスを返しても、列挙したパスにそろえる。
    scanner_.set_plugins("/a.vst3", {plugin("A", "/other/A.vst3")});
    catalog_.load();
    scan_all(catalog_);
    ASSERT_EQ(catalog_.available_plugins().size(), 1U);
    EXPECT_EQ(catalog_.available_plugins()[0].file_path, "/a.vst3");
}

TEST_F(PluginCatalogTest, StartsEmptyWhenTheSavedRecordsCannotBeRead) {
    store_.fail_loads_with(SettingsError::kCorrupted);
    scanner_.set_files({file("/a.vst3")});
    catalog_.load();
    EXPECT_TRUE(catalog_.records().empty());
    EXPECT_EQ(catalog_.begin_rescan(), 1U);
}

TEST_F(PluginCatalogTest, IgnoresDuplicatedPathsInTheSavedRecords) {
    store_.set_records(
        {PluginScanRecord{file("/a.vst3"), PluginScanStatus::kScanned, {plugin("A", "/a.vst3")}},
         PluginScanRecord{file("/a.vst3"), PluginScanStatus::kScanned, {plugin("A", "/a.vst3")}}});
    catalog_.load();
    EXPECT_EQ(catalog_.records().size(), 1U);
    EXPECT_EQ(catalog_.available_plugins().size(), 1U);
}

}  // namespace
}  // namespace vocroz::session
