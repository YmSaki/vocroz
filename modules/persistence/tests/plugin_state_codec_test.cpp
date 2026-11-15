// SPDX-License-Identifier: AGPL-3.0-only
//
// プラグインの状態の JSON の変換のテスト。往復できることと、壊れたファイルや大きすぎる状態を
// 受け付けないことを確かめる。

#include "plugin_state_codec.h"

#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace vocroz::persistence {
namespace {

std::vector<session::SavedPluginState> two_states() {
    return {{"fx-1", "VST3-EQ", {0x00, 0x01, 0xFE, 0xFF}}, {"fx-2", "VST3-Comp", {}}};
}

TEST(PluginStateCodecTest, RoundTrips) {
    const auto decoded = decode_plugin_states(encode_plugin_states(two_states()));
    ASSERT_TRUE(decoded);
    EXPECT_EQ(*decoded, two_states());
}

TEST(PluginStateCodecTest, RoundTripsNoStates) {
    const auto decoded = decode_plugin_states(encode_plugin_states({}));
    ASSERT_TRUE(decoded);
    EXPECT_TRUE(decoded->empty());
}

TEST(PluginStateCodecTest, RejectsBrokenFiles) {
    EXPECT_FALSE(decode_plugin_states(""));
    EXPECT_FALSE(decode_plugin_states("[]"));
    EXPECT_FALSE(decode_plugin_states(R"({"schema_version":2,"plugin_states":[]})"));
    EXPECT_FALSE(decode_plugin_states(R"({"schema_version":1})"));
    EXPECT_FALSE(decode_plugin_states(
        R"({"schema_version":1,"plugin_states":[{"insert_id":"fx-1","plugin_id":"p","state":"!"}]})"));
    EXPECT_FALSE(decode_plugin_states(
        R"({"schema_version":1,"plugin_states":[{"insert_id":"","plugin_id":"p","state":""}]})"));
    EXPECT_FALSE(decode_plugin_states(
        R"({"schema_version":1,"plugin_states":[{"insert_id":"fx-1","state":""}]})"));
}

TEST(PluginStateCodecTest, RejectsStatesLargerThanTheLimit) {
    std::vector<session::SavedPluginState> large{
        {"fx-1", "VST3-EQ", std::vector<std::uint8_t>(session::kMaxPluginStateBytes + 1, 0x5A)}};
    EXPECT_FALSE(decode_plugin_states(encode_plugin_states(large)));
    large[0].state.pop_back();
    EXPECT_TRUE(decode_plugin_states(encode_plugin_states(large)));
}

}  // namespace
}  // namespace vocroz::persistence
