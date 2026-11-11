// SPDX-License-Identifier: AGPL-3.0-only
//
// 挿入ごとの読み込みの状態のテスト。適用中の構成に合わせた読み込みと破棄の依頼、遅れて届いた
// 結果の扱い、読み込みと処理の失敗、グラフに渡す挿入の選び方を確かめる。

#include "plugin_insert_book.h"

#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "fakes/fake_plugin_processor.h"
#include "fakes/fake_plugin_rack.h"

namespace vocroz::session {
namespace {

namespace cp = control_protocol;

PluginDescription plugin(const std::string& id) {
    PluginDescription d;
    d.name = id;
    d.identifier = id;
    d.input_channels = 2;
    d.output_channels = 2;
    return d;
}

const std::vector<PluginDescription> kPlugins{plugin("eq"), plugin("comp")};

std::shared_ptr<audio_core::PluginProcessor> processor() {
    return std::make_shared<audio_core::FakePluginProcessor>();
}

class PluginInsertBookTest : public ::testing::Test {
protected:
    FakePluginRack rack_;
    PluginInsertBook book_{rack_};
};

TEST_F(PluginInsertBookTest, RequestsLoadsForNewInsertsAndKeepsThemOutOfTheGraphUntilLoaded) {
    const std::vector<cp::RoutingInsert> inserts{{"fx-1", "ch1", "eq"}, {"fx-2", "master", "comp"}};
    book_.reconcile(inserts, kPlugins);
    ASSERT_EQ(rack_.loads().size(), 2U);
    EXPECT_EQ(rack_.loads()[0].insert_id, "fx-1");
    EXPECT_EQ(rack_.loads()[0].plugin_id, "eq");
    EXPECT_NE(rack_.loads()[0].ticket, rack_.loads()[1].ticket);
    EXPECT_EQ(book_.find("fx-1")->state, InsertLoadState::kLoading);
    EXPECT_TRUE(book_.ready_for(inserts).empty());

    const auto fx1 = processor();
    EXPECT_TRUE(book_.on_loaded("fx-1", rack_.loads()[0].ticket, fx1));
    const auto ready = book_.ready_for(inserts);
    ASSERT_EQ(ready.size(), 1U);
    EXPECT_EQ(ready[0].entity_id, "fx-1");
    EXPECT_EQ(ready[0].processor, fx1);
    ASSERT_NE(ready[0].fault, nullptr);

    // 同じ構成でもう一度合わせても、読み込み直さない。
    book_.reconcile(inserts, kPlugins);
    EXPECT_EQ(rack_.loads().size(), 2U);
    EXPECT_TRUE(rack_.releases().empty());
}

TEST_F(PluginInsertBookTest, ReleasesRemovedInsertsAndReloadsWhenThePluginChanges) {
    book_.reconcile(std::vector<cp::RoutingInsert>{{"fx-1", "ch1", "eq"}, {"fx-2", "ch1", "eq"}},
                    kPlugins);
    book_.reconcile(std::vector<cp::RoutingInsert>{{"fx-1", "ch1", "comp"}}, kPlugins);
    EXPECT_EQ(rack_.releases(), (std::vector<std::string>{"fx-1", "fx-2"}));
    ASSERT_EQ(rack_.loads().size(), 3U);
    EXPECT_EQ(rack_.loads()[2].insert_id, "fx-1");
    EXPECT_EQ(rack_.loads()[2].plugin_id, "comp");
    EXPECT_EQ(book_.find("fx-2"), nullptr);
}

TEST_F(PluginInsertBookTest, IgnoresResultsOfEarlierRequests) {
    const std::vector<cp::RoutingInsert> eq{{"fx-1", "ch1", "eq"}};
    book_.reconcile(eq, kPlugins);
    const auto first = rack_.loads()[0].ticket;
    book_.reconcile({}, kPlugins);
    book_.reconcile(eq, kPlugins);

    // 外す前の依頼の結果が遅れて届いても、今の依頼の結果として使わない。
    EXPECT_FALSE(book_.on_loaded("fx-1", first, processor()));
    EXPECT_EQ(book_.find("fx-1")->state, InsertLoadState::kLoading);
    EXPECT_TRUE(book_.on_loaded("fx-1", rack_.loads()[1].ticket, processor()));
    // 済んだ依頼の結果がもう一度届いても受けない。
    EXPECT_FALSE(book_.on_loaded("fx-1", rack_.loads()[1].ticket, processor()));
    EXPECT_FALSE(book_.on_loaded("fx-9", 99, processor()));
}

TEST_F(PluginInsertBookTest, LoadFailuresKeepTheInsertOutOfTheGraph) {
    const std::vector<cp::RoutingInsert> inserts{{"fx-1", "ch1", "eq"}};
    book_.reconcile(inserts, kPlugins);
    EXPECT_TRUE(book_.on_loaded("fx-1", rack_.loads()[0].ticket,
                                common::make_unexpected(PluginLoadError::kUnsupportedLayout)));
    const auto* slot = book_.find("fx-1");
    EXPECT_EQ(slot->state, InsertLoadState::kFailed);
    EXPECT_EQ(slot->failure, InsertFailure::kUnsupportedLayout);
    EXPECT_TRUE(book_.ready_for(inserts).empty());
}

TEST_F(PluginInsertBookTest, InsertsOfPluginsMissingFromTheListFailWithoutLoading) {
    book_.reconcile(std::vector<cp::RoutingInsert>{{"fx-1", "ch1", "gone"}}, kPlugins);
    EXPECT_TRUE(rack_.loads().empty());
    EXPECT_EQ(book_.find("fx-1")->failure, InsertFailure::kNotFound);
}

TEST_F(PluginInsertBookTest, ProcessingFaultsFailTheInsertOnce) {
    const std::vector<cp::RoutingInsert> inserts{{"fx-1", "ch1", "eq"}};
    book_.reconcile(inserts, kPlugins);
    ASSERT_TRUE(book_.on_loaded("fx-1", rack_.loads()[0].ticket, processor()));
    EXPECT_TRUE(book_.collect_faults().empty());

    book_.ready_for(inserts)[0].fault->raise();
    EXPECT_EQ(book_.collect_faults(), std::vector<std::string>{"fx-1"});
    EXPECT_TRUE(book_.collect_faults().empty());
    EXPECT_EQ(book_.find("fx-1")->failure, InsertFailure::kProcessingFailed);
    EXPECT_TRUE(book_.ready_for(inserts).empty());
}

TEST_F(PluginInsertBookTest, OnlyInsertsMatchingTheConfigGoToTheGraph) {
    book_.reconcile(std::vector<cp::RoutingInsert>{{"fx-1", "ch1", "eq"}}, kPlugins);
    ASSERT_TRUE(book_.on_loaded("fx-1", rack_.loads()[0].ticket, processor()));
    // ステージング中の構成で同じ entity_id に別のプラグインを挿すなら、前の処理は使わない。
    EXPECT_TRUE(book_.ready_for(std::vector<cp::RoutingInsert>{{"fx-1", "ch1", "comp"}}).empty());
    EXPECT_EQ(book_.ready_for(std::vector<cp::RoutingInsert>{{"fx-1", "master", "eq"}}).size(), 1U);
}

}  // namespace
}  // namespace vocroz::session
