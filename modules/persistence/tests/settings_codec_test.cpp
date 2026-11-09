// SPDX-License-Identifier: AGPL-3.0-only
//
// 設定ファイルの JSON の変換のテスト。往復で値が変わらないことと、
// 形の合わない入力（壊れたファイル、別の形式のバージョン、型の違い）を拒否することを確かめる。

#include "settings_codec.h"

#include <gtest/gtest.h>

namespace vocroz::persistence {
namespace {

session::HostSettings sample_host() {
    session::HostSettings s;
    s.session_name = "日曜の合奏";
    s.output_device_id = "{0.0.0.00000000}.{abc}";
    s.master_gain_db = -6.5;
    namespace cp = control_protocol;
    s.routing.buses.push_back(cp::RoutingBus{"bus-1", "返し", cp::BusKind::kCue});
    s.routing.buses.push_back(cp::RoutingBus{"bus-2", "FX", cp::BusKind::kUser});
    s.routing.outputs.push_back(cp::RoutingOutput{"out-1", "IEM", 4});
    s.routing.sends.push_back(cp::RoutingSend{"send-1", "bus-1", "out-1", cp::SendTap::kPostFader});
    s.routing.sends.push_back(cp::RoutingSend{"send-2", "bus-2", "bus-1", cp::SendTap::kPreFader});
    s.routing.delays.push_back(cp::RoutingDelay{"delay-1", 12.5});
    s.routing.input_delays.push_back(cp::InputDelay{"ch1", 3.0});
    s.route_gains = {{"bus-1", -3.0}, {"send-1", std::nullopt}};
    return s;
}

session::ClientSettings sample_client() {
    session::ClientSettings s;
    s.display_name = "Vo. さとう";
    s.selected_input_id = "{0.0.1.00000000}.{def}";
    s.send_level_db = 3.0;
    return s;
}

TEST(SettingsCodecTest, HostSettingsRoundTrip) {
    const auto decoded = decode_host_settings(encode_host_settings(sample_host()));
    ASSERT_TRUE(decoded);
    EXPECT_EQ(*decoded, sample_host());
}

TEST(SettingsCodecTest, HostSettingsRoundTripWithNullFields) {
    session::HostSettings s;
    s.output_device_id = std::nullopt;
    s.master_gain_db = std::nullopt;  // -∞ dB
    const auto decoded = decode_host_settings(encode_host_settings(s));
    ASSERT_TRUE(decoded);
    EXPECT_EQ(*decoded, s);
}

TEST(SettingsCodecTest, ClientSettingsRoundTrip) {
    const auto decoded = decode_client_settings(encode_client_settings(sample_client()));
    ASSERT_TRUE(decoded);
    EXPECT_EQ(*decoded, sample_client());
}

TEST(SettingsCodecTest, ClientSettingsRoundTripWithNoInput) {
    auto s = sample_client();
    s.selected_input_id = std::nullopt;
    const auto decoded = decode_client_settings(encode_client_settings(s));
    ASSERT_TRUE(decoded);
    EXPECT_EQ(*decoded, s);
}

TEST(SettingsCodecTest, RejectsTruncatedJson) {
    const auto text = encode_host_settings(sample_host());
    EXPECT_FALSE(decode_host_settings(text.substr(0, text.size() / 2)));
}

TEST(SettingsCodecTest, RejectsGarbageAndEmptyText) {
    EXPECT_FALSE(decode_host_settings(""));
    EXPECT_FALSE(decode_host_settings(std::string(16, '\0')));
    EXPECT_FALSE(decode_client_settings("[1, 2, 3]"));
}

TEST(SettingsCodecTest, RejectsUnknownSchemaVersion) {
    EXPECT_FALSE(decode_host_settings(
        R"({"schema_version":2,"session_name":"","output_device_id":null,"master_gain_db":0})"));
    EXPECT_FALSE(
        decode_host_settings(R"({"session_name":"","output_device_id":null,"master_gain_db":0})"));
}

TEST(SettingsCodecTest, RejectsWrongTypes) {
    EXPECT_FALSE(decode_host_settings(
        R"({"schema_version":1,"session_name":1,"output_device_id":null,"master_gain_db":0})"));
    EXPECT_FALSE(decode_host_settings(
        R"({"schema_version":1,"session_name":"","output_device_id":3,"master_gain_db":0})"));
    EXPECT_FALSE(decode_host_settings(
        R"({"schema_version":1,"session_name":"","output_device_id":null,"master_gain_db":"0"})"));
    EXPECT_FALSE(decode_client_settings(
        R"({"schema_version":1,"display_name":"a","selected_input_id":null,"send_level_db":null})"));
}

TEST(SettingsCodecTest, RejectsMissingFields) {
    EXPECT_FALSE(decode_host_settings(R"({"schema_version":1,"session_name":""})"));
    EXPECT_FALSE(decode_client_settings(R"({"schema_version":1,"display_name":"a"})"));
}

TEST(SettingsCodecTest, IgnoresUnknownFields) {
    // 知らないフィールドは無視する（形式のバージョンを上げずに足せるようにするため）。
    const auto decoded = decode_client_settings(
        R"({"schema_version":1,"display_name":"a","selected_input_id":null,"send_level_db":-3,"extra":true})");
    ASSERT_TRUE(decoded);
    EXPECT_EQ(decoded->send_level_db, -3.0);
}

TEST(SettingsCodecTest, ReadsHostSettingsSavedBeforeRoutingWasPersisted) {
    const auto decoded = decode_host_settings(
        R"({"schema_version":1,"session_name":"a","output_device_id":null,"master_gain_db":-3})");
    ASSERT_TRUE(decoded);
    EXPECT_EQ(decoded->session_name, "a");
    EXPECT_EQ(decoded->routing, control_protocol::RoutingConfig{});
    EXPECT_TRUE(decoded->route_gains.empty());
}

TEST(SettingsCodecTest, RejectsMalformedRouting) {
    const std::string head =
        R"({"schema_version":1,"session_name":"","output_device_id":null,"master_gain_db":0,)";
    const std::string empty_lists = R"("outputs":[],"sends":[],"delays":[],"input_delays":[])";
    EXPECT_TRUE(decode_host_settings(head + R"("routing":{"buses":[],)" + empty_lists +
                                     R"(},"route_gains":[]})"));
    // 知らないバスの種類
    EXPECT_FALSE(decode_host_settings(
        head + R"("routing":{"buses":[{"entity_id":"bus-1","name":"","kind":"aux"}],)" +
        empty_lists + R"(},"route_gains":[]})"));
    // 一覧が欠けている
    EXPECT_FALSE(decode_host_settings(head + R"("routing":{"buses":[]},"route_gains":[]})"));
    // フェーダーの値だけがある
    EXPECT_FALSE(decode_host_settings(head + R"("route_gains":[]})"));
    // 負の出力チャンネル
    EXPECT_FALSE(decode_host_settings(
        head +
        R"("routing":{"buses":[],"outputs":[{"entity_id":"out-1","name":"","device_channel":-2}],"sends":[],"delays":[],"input_delays":[]},"route_gains":[]})"));
}

}  // namespace
}  // namespace vocroz::persistence
