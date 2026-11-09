// SPDX-License-Identifier: AGPL-3.0-only
//
// make_helper_plugin_scanner() の単体テスト。
// フェイクのヘルパー（tests/fake_plugin_scanner.cpp）で、次のことを検証する。
// ヘルパーの失敗の理由が、session の区分に写ること。
// PluginCatalog と組み合わせると、落ちるファイルを読み込み不可として記録し、スキャンを続けること。

#include <chrono>
#include <filesystem>
#include <memory>
#include <vector>

#include <gtest/gtest.h>

#include "vocroz/plugin_host/factories.h"
#include "vocroz/session/plugin_catalog.h"

#include "fakes/fake_plugin_catalog_store.h"

namespace vocroz::plugin_host {
namespace {

using namespace std::chrono_literals;

const std::filesystem::path kHelper{VOCROZ_FAKE_PLUGIN_SCANNER};

// 列挙を通さず、パスを決めたファイルをスキャンさせるためのスキャナー。
// フェイクのヘルパーは拡張子のないファイル名（crash など）で振る舞いを変えるので、
// .vst3 だけを列挙する list_plugin_files() では、そのファイルを渡せないため。
class ListedScanner final : public session::PluginScanner {
public:
    ListedScanner(std::unique_ptr<session::PluginScanner> inner,
                  std::vector<session::PluginFile> files)
        : inner_(std::move(inner)), files_(std::move(files)) {}

    std::vector<session::PluginFile> list_plugin_files() override {
        return files_;
    }
    common::Result<std::vector<session::PluginDescription>, session::PluginScanFailure> scan(
        const session::PluginFile& file) override {
        return inner_->scan(file);
    }

private:
    std::unique_ptr<session::PluginScanner> inner_;
    std::vector<session::PluginFile> files_;
};

session::PluginFile file(const char* path) {
    return session::PluginFile{path, 1, 1};
}

TEST(JuceHelperPluginScannerTest, MapsHelperFailuresToScanFailures) {
    auto scanner = make_helper_plugin_scanner(kHelper, {}, 10s);
    EXPECT_EQ(scanner->scan(file("crash")).error(), session::PluginScanFailure::kCrashed);
    EXPECT_EQ(scanner->scan(file("garbage")).error(), session::PluginScanFailure::kInvalidOutput);
    auto ok = scanner->scan(file("Reverb.vst3"));
    ASSERT_TRUE(ok.has_value());
    ASSERT_EQ(ok.value().size(), 1U);
    EXPECT_EQ(ok.value()[0].name, "Reverb.vst3");

    auto missing = make_helper_plugin_scanner(kHelper.parent_path() / "no-such-helper", {}, 10s);
    EXPECT_EQ(missing->scan(file("Reverb.vst3")).error(),
              session::PluginScanFailure::kScannerUnavailable);
}

TEST(JuceHelperPluginScannerTest, CatalogKeepsScanningPastAPluginThatCrashesOrHangs) {
    ListedScanner scanner(
        make_helper_plugin_scanner(kHelper, {}, 500ms),
        {file("/plugins/crash"), file("/plugins/hang"), file("/plugins/Eq.vst3")});
    session::FakePluginCatalogStore store;
    session::PluginCatalog catalog(scanner, store);
    catalog.load();
    catalog.begin_rescan();
    while (catalog.scan_next() == session::PluginScanProgress::kMore) {
    }

    const auto plugins = catalog.available_plugins();
    ASSERT_EQ(plugins.size(), 1U);
    EXPECT_EQ(plugins[0].name, "Eq.vst3");
    // 記録はパスの順（大文字が先）: Eq.vst3、crash、hang。
    ASSERT_EQ(catalog.records().size(), 3U);
    EXPECT_EQ(catalog.records()[0].status, session::PluginScanStatus::kScanned);
    EXPECT_EQ(catalog.records()[1].status, session::PluginScanStatus::kCrashed);
    EXPECT_EQ(catalog.records()[2].status, session::PluginScanStatus::kTimedOut);
}

}  // namespace
}  // namespace vocroz::plugin_host
