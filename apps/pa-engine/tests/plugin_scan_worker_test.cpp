// SPDX-License-Identifier: AGPL-3.0-only
//
// PluginScanWorker の単体テスト。実際にスキャンのスレッドを立て、フェイクのスキャナーと記録の
// 保存先で、起動時の記録の読み込み、スキャンが済んだときの一覧の通知、スキャンし直しの要求、
// ヘルパーを起動できないときの中断を確かめる。

#include "plugin_scan_worker.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "fakes/fake_logger.h"
#include "fakes/fake_plugin_catalog_store.h"
#include "fakes/fake_plugin_scanner.h"

namespace vocroz::engine {
namespace {

namespace session = vocroz::session;

// スキャンのスレッドを待つ上限。テストが固まらないためだけのもので、普段はすぐ済む。
constexpr auto kWaitLimit = std::chrono::seconds{10};

session::PluginDescription plugin(const std::string& name, const std::string& path) {
    session::PluginDescription d;
    d.name = name;
    d.identifier = "VST3-" + name;
    d.file_path = path;
    return d;
}

class PluginScanWorkerTest : public ::testing::Test {
protected:
    PluginScanWorkerTest() {
        scanner_.set_files({{"C:/VST3/eq.vst3", 1, 10}});
        scanner_.set_plugins("C:/VST3/eq.vst3", {plugin("EQ", "C:/VST3/eq.vst3")});
    }

    /// publish された一覧が count 個になるまで待つ。
    /// @return 待てたら true
    bool wait_for_publishes(std::size_t count) {
        std::unique_lock lock{mutex_};
        return published_cv_.wait_for(lock, kWaitLimit, [&] { return published_.size() >= count; });
    }

    std::vector<std::vector<session::PluginDescription>> published() {
        const std::lock_guard lock{mutex_};
        return published_;
    }

    std::vector<std::vector<session::PluginScanRecord>> blocked() {
        const std::lock_guard lock{mutex_};
        return blocked_;
    }

    int started() {
        const std::lock_guard lock{mutex_};
        return started_;
    }

    PluginScanWorker::Listener publish() {
        return {[this] {
                    const std::lock_guard lock{mutex_};
                    ++started_;
                },
                [this](PluginScanWorker::Catalog catalog) {
                    {
                        const std::lock_guard lock{mutex_};
                        published_.push_back(std::move(catalog.plugins));
                        blocked_.push_back(std::move(catalog.blocked));
                    }
                    published_cv_.notify_all();
                }};
    }

    session::FakePluginScanner scanner_;
    session::FakePluginCatalogStore store_;
    common::FakeLogger logger_;
    std::mutex mutex_;
    std::condition_variable published_cv_;
    std::vector<std::vector<session::PluginDescription>> published_;
    std::vector<std::vector<session::PluginScanRecord>> blocked_;
    int started_ = 0;
};

TEST_F(PluginScanWorkerTest, LoadSavedReturnsThePluginsOfTheSavedRecords) {
    store_.set_records({{{"C:/VST3/eq.vst3", 1, 10},
                         session::PluginScanStatus::kScanned,
                         {plugin("EQ", "C:/VST3/eq.vst3")}}});
    PluginScanWorker worker{scanner_, store_, publish(), logger_};

    const auto catalog = worker.load_saved();

    ASSERT_EQ(catalog.plugins.size(), 1U);
    EXPECT_EQ(catalog.plugins[0].identifier, "VST3-EQ");
    EXPECT_TRUE(catalog.blocked.empty());
    // 読むだけで、スキャンはしない。
    EXPECT_TRUE(scanner_.scanned().empty());
}

TEST_F(PluginScanWorkerTest, LoadSavedReturnsNothingWithoutRecords) {
    PluginScanWorker worker{scanner_, store_, publish(), logger_};

    const auto catalog = worker.load_saved();
    EXPECT_TRUE(catalog.plugins.empty());
    EXPECT_TRUE(catalog.blocked.empty());
}

TEST_F(PluginScanWorkerTest, PublishesThePluginsWhenTheFirstScanFinishes) {
    PluginScanWorker worker{scanner_, store_, publish(), logger_};
    static_cast<void>(worker.load_saved());

    worker.start();

    ASSERT_TRUE(wait_for_publishes(1));
    worker.stop();
    const auto lists = published();
    ASSERT_EQ(lists.size(), 1U);
    ASSERT_EQ(lists[0].size(), 1U);
    EXPECT_EQ(lists[0][0].identifier, "VST3-EQ");
    EXPECT_EQ(started(), 1);
    // 済んだスキャンの記録を保存する。
    EXPECT_EQ(store_.save_count(), 1);
}

TEST_F(PluginScanWorkerTest, SkipsUnchangedFilesFromTheSavedRecords) {
    store_.set_records({{{"C:/VST3/eq.vst3", 1, 10},
                         session::PluginScanStatus::kScanned,
                         {plugin("EQ", "C:/VST3/eq.vst3")}}});
    PluginScanWorker worker{scanner_, store_, publish(), logger_};
    static_cast<void>(worker.load_saved());

    worker.start();

    ASSERT_TRUE(wait_for_publishes(1));
    worker.stop();
    EXPECT_TRUE(scanner_.scanned().empty());
    ASSERT_EQ(published()[0].size(), 1U);
}

TEST_F(PluginScanWorkerTest, RescansWhenRequested) {
    PluginScanWorker worker{scanner_, store_, publish(), logger_};
    static_cast<void>(worker.load_saved());
    worker.start();
    ASSERT_TRUE(wait_for_publishes(1));

    worker.request_rescan();

    ASSERT_TRUE(wait_for_publishes(2));
    worker.stop();
    EXPECT_EQ(published().size(), 2U);
}

TEST_F(PluginScanWorkerTest, StopsTheScanAndLogsWhenTheHelperCannotStart) {
    scanner_.fail("C:/VST3/eq.vst3", session::PluginScanFailure::kScannerUnavailable);
    PluginScanWorker worker{scanner_, store_, publish(), logger_};
    static_cast<void>(worker.load_saved());

    worker.start();

    ASSERT_TRUE(wait_for_publishes(1));
    worker.stop();
    // 読み込み不可として記録しないので、一覧は空で、記録も保存しない。
    EXPECT_TRUE(published()[0].empty());
    EXPECT_EQ(store_.save_count(), 0);
    const auto& entries = logger_.entries();
    EXPECT_TRUE(std::any_of(entries.begin(), entries.end(), [](const auto& entry) {
        return entry.level == common::LogLevel::kError;
    }));
}

TEST_F(PluginScanWorkerTest, ReportsFilesThatCrashedTheScan) {
    scanner_.fail("C:/VST3/eq.vst3", session::PluginScanFailure::kCrashed);
    PluginScanWorker worker{scanner_, store_, publish(), logger_};
    static_cast<void>(worker.load_saved());

    worker.start();

    ASSERT_TRUE(wait_for_publishes(1));
    worker.stop();
    EXPECT_TRUE(published()[0].empty());
    const auto files = blocked();
    ASSERT_EQ(files[0].size(), 1U);
    EXPECT_EQ(files[0][0].file.path, "C:/VST3/eq.vst3");
    EXPECT_EQ(files[0][0].status, session::PluginScanStatus::kCrashed);
}

TEST_F(PluginScanWorkerTest, StopWithoutStartDoesNothing) {
    PluginScanWorker worker{scanner_, store_, publish(), logger_};

    worker.stop();

    EXPECT_TRUE(published().empty());
}

}  // namespace
}  // namespace vocroz::engine
