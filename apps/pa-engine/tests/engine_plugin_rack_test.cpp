// SPDX-License-Identifier: AGPL-3.0-only
//
// EnginePluginRack の単体テスト。メッセージスレッドの代わりに、積んだ処理をテストが順に
// 実行するキューを使い、読み込みの結果の返し方と、インスタンスを破棄する時機（release() の後で、
// グラフが処理を手放してから、メッセージスレッドで）を確かめる。

#include "engine_plugin_rack.h"

#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "fakes/fake_logger.h"
#include "fakes/fake_plugin_editor_host.h"
#include "fakes/fake_plugin_loader.h"
#include "fakes/fake_plugin_processor.h"

namespace vocroz::engine {
namespace {

namespace session = vocroz::session;

session::PluginDescription eq() {
    session::PluginDescription d;
    d.name = "EQ";
    d.identifier = "VST3-EQ-1";
    return d;
}

class EnginePluginRackTest : public ::testing::Test {
protected:
    /// 届いた結果 1 つ。
    struct Delivered {
        std::string insert_id;
        session::PluginLoadTicket ticket = 0;
        EnginePluginRack::LoadResult result;
    };

    EnginePluginRackTest() {
        // ローダーとエディターのホストは最初の読み込みでラックへ移るので、テストは生の
        // ポインターで観察する。
        loader_ = owned_loader_.get();
        editors_ = owned_editors_.get();
        rack_.emplace(EnginePluginRack::Ports{
            [this](EnginePluginRack::Task task) {
                if (!running_) {
                    return false;
                }
                tasks_.push_back(std::move(task));
                return true;
            },
            [this]() -> std::unique_ptr<session::PluginLoader> { return std::move(owned_loader_); },
            [this]() -> std::unique_ptr<session::PluginEditorHost> {
                return std::move(owned_editors_);
            },
            [this](const std::string& insert_id, session::PluginLoadTicket ticket,
                   EnginePluginRack::LoadResult result) {
                delivered_.push_back(Delivered{insert_id, ticket, std::move(result)});
            },
            [this](const std::string& insert_id) { editor_failures_.push_back(insert_id); },
            logger_});
    }

    /// メッセージスレッドに積まれた処理をすべて実行する。
    void run_message_thread() {
        while (!tasks_.empty()) {
            auto task = std::move(tasks_.front());
            tasks_.pop_front();
            task();
        }
    }

    std::shared_ptr<audio_core::PluginProcessor> load_eq(const std::string& id,
                                                         session::PluginLoadTicket ticket) {
        rack_->load(id, ticket, eq());
        run_message_thread();
        EXPECT_FALSE(delivered_.empty());
        auto result = std::move(delivered_.back().result);
        EXPECT_TRUE(result);
        return result ? std::move(result).value() : nullptr;
    }

    common::FakeLogger logger_;
    std::deque<EnginePluginRack::Task> tasks_;
    bool running_ = true;
    std::vector<Delivered> delivered_;
    std::vector<std::string> editor_failures_;
    session::FakePluginLoader* loader_ = nullptr;
    session::FakePluginEditorHost* editors_ = nullptr;
    std::unique_ptr<session::FakePluginLoader> owned_loader_ =
        std::make_unique<session::FakePluginLoader>(
            [] { return std::make_shared<audio_core::FakePluginProcessor>(); });
    std::unique_ptr<session::FakePluginEditorHost> owned_editors_ =
        std::make_unique<session::FakePluginEditorHost>();
    std::optional<EnginePluginRack> rack_;
};

TEST_F(EnginePluginRackTest, LoadsOnTheMessageThreadAndDeliversTheProcessor) {
    rack_->load("fx-1", 7, eq());
    // 依頼しただけでは読み込まない（コントロールスレッドを止めない）。
    EXPECT_TRUE(delivered_.empty());
    run_message_thread();
    ASSERT_EQ(delivered_.size(), 1U);
    EXPECT_EQ(delivered_[0].insert_id, "fx-1");
    EXPECT_EQ(delivered_[0].ticket, 7U);
    ASSERT_TRUE(delivered_[0].result);
    EXPECT_NE(delivered_[0].result.value(), nullptr);
    EXPECT_EQ(loader_->loaded(), std::vector<std::string>{"VST3-EQ-1"});
}

TEST_F(EnginePluginRackTest, DeliversLoadFailures) {
    // 最初の読み込みでローダーを作るので、先に 1 回読み込んでから失敗させる。
    std::ignore = load_eq("fx-0", 1);
    loader_->fail_with(session::PluginLoadError::kUnsupportedLayout);
    rack_->load("fx-1", 2, eq());
    run_message_thread();
    ASSERT_FALSE(delivered_.back().result);
    EXPECT_EQ(delivered_.back().result.error(), session::PluginLoadError::kUnsupportedLayout);
}

TEST_F(EnginePluginRackTest, DestroysAReleasedInstanceOnlyAfterTheGraphLetsGo) {
    auto processor = load_eq("fx-1", 1);
    EXPECT_EQ(loader_->alive(), 1);
    rack_->release("fx-1");
    run_message_thread();
    // グラフがまだ処理を持っているので、インスタンスは残す。
    EXPECT_EQ(loader_->alive(), 1);

    processor.reset();
    // 手放したことはメッセージスレッドへ積むだけで、その場では破棄しない。
    EXPECT_EQ(loader_->alive(), 1);
    run_message_thread();
    EXPECT_EQ(loader_->alive(), 0);
}

TEST_F(EnginePluginRackTest, KeepsAnInstanceTheGraphDroppedUntilReleased) {
    auto processor = load_eq("fx-1", 1);
    processor.reset();
    run_message_thread();
    EXPECT_EQ(loader_->alive(), 1);
    rack_->release("fx-1");
    run_message_thread();
    EXPECT_EQ(loader_->alive(), 0);
}

TEST_F(EnginePluginRackTest, ReloadingTheSameInsertKeepsTheInstancesApart) {
    auto first = load_eq("fx-1", 1);
    rack_->release("fx-1");
    auto second = load_eq("fx-1", 2);
    EXPECT_EQ(loader_->alive(), 2);
    // 前のインスタンスを手放しても、今のインスタンスは残る。
    first.reset();
    run_message_thread();
    EXPECT_EQ(loader_->alive(), 1);
    rack_->release("fx-1");
    second.reset();
    run_message_thread();
    EXPECT_EQ(loader_->alive(), 0);
}

TEST_F(EnginePluginRackTest, DestroyAllDestroysEverythingAndStopsPosting) {
    auto processor = load_eq("fx-1", 1);
    rack_->destroy_all();
    EXPECT_EQ(loader_->alive(), 0);
    // JUCE を終えた後にグラフが処理を手放しても、メッセージスレッドへは積まない。
    processor.reset();
    EXPECT_TRUE(tasks_.empty());
}

TEST_F(EnginePluginRackTest, ClosesTheEditorWhenReleased) {
    auto processor = load_eq("fx-1", 1);
    const auto closes = editors_->close_count();
    rack_->release("fx-1");
    run_message_thread();
    // グラフが処理を持っていても、外した挿入のエディターはすぐに閉じる。
    EXPECT_GT(editors_->close_count(), closes);
}

TEST_F(EnginePluginRackTest, ReportsLoadsWhenTheMessageThreadIsStopped) {
    running_ = false;
    rack_->load("fx-1", 1, eq());
    EXPECT_TRUE(delivered_.empty());
    EXPECT_FALSE(logger_.entries().empty());
}

TEST_F(EnginePluginRackTest, OpensTheEditorOfALoadedInsert) {
    auto processor = load_eq("fx-1", 1);

    rack_->open_editor("fx-1", "EQ");
    run_message_thread();

    EXPECT_EQ(editors_->open_count(), 1U);
    EXPECT_TRUE(editor_failures_.empty());
}

TEST_F(EnginePluginRackTest, ReportsAnEditorThatCannotOpen) {
    auto processor = load_eq("fx-1", 1);
    editors_->fail_with(session::PluginEditorError::kOpenFailed);

    rack_->open_editor("fx-1", "EQ");
    run_message_thread();

    EXPECT_EQ(editors_->open_count(), 0U);
    EXPECT_EQ(editor_failures_, std::vector<std::string>{"fx-1"});
}

TEST_F(EnginePluginRackTest, DoesNotOpenTheEditorOfAReleasedInsert) {
    auto processor = load_eq("fx-1", 1);

    rack_->release("fx-1");
    rack_->open_editor("fx-1", "EQ");
    run_message_thread();

    EXPECT_EQ(editors_->open_count(), 0U);
    EXPECT_TRUE(editor_failures_.empty());
}

}  // namespace
}  // namespace vocroz::engine
