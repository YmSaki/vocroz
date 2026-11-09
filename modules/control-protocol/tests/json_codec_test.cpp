// SPDX-License-Identifier: AGPL-3.0-only
//
// json_codec.h
// の単体テスト。信頼境界として不正な入力を拒否すること、範囲の境界の値を受け付けること、
// エンコードが JSON で表せない値を安全に扱うことを確かめる。全メッセージの往復は
// json_codec_examples_test.cpp。

#include "vocroz/control_protocol/json_codec.h"

#include <cmath>
#include <limits>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "vocroz/control_protocol/constants.h"

namespace vocroz::control_protocol {
namespace {

/// 正しい set_channel_mute。テストごとに 1 か所だけ壊して使う。
nlohmann::json valid_mute() {
    return {{"type", "set_channel_mute"},
            {"request_id", "r1"},
            {"entity_id", "ch1"},
            {"expected_revision", 3},
            {"muted", true}};
}

/// 正しい set_gain。
nlohmann::json valid_gain() {
    return {{"type", "set_gain"}, {"entity_id", "ch1"}, {"seq", 1}, {"gain_db", 0.0}};
}

DecodeError ui_error(const std::string& text) {
    const auto result = decode_ui_to_engine(text);
    EXPECT_FALSE(result) << text;
    return result ? DecodeError{} : result.error();
}

DecodeError ui_error(const nlohmann::json& value) {
    return ui_error(value.dump());
}

DecodeError engine_error(const nlohmann::json& value) {
    const auto result = decode_engine_to_ui(value.dump());
    EXPECT_FALSE(result) << value.dump();
    return result ? DecodeError{} : result.error();
}

TEST(JsonCodecTest, DecodesDiscreteCommand) {
    const auto result = decode_ui_to_engine(valid_mute().dump());
    ASSERT_TRUE(result);
    const auto* command = std::get_if<DiscreteCommand>(&result.value());
    ASSERT_NE(command, nullptr);
    EXPECT_EQ(*command, (DiscreteCommand{"r1", "ch1", 3, SetChannelMute{true}}));
}

TEST(JsonCodecTest, DecodesNullGainAsNegativeInfinity) {
    auto message = valid_gain();
    message["gain_db"] = nullptr;
    const auto result = decode_ui_to_engine(message.dump());
    ASSERT_TRUE(result);
    EXPECT_EQ(std::get<SetGain>(result.value()), (SetGain{"ch1", 1, std::nullopt}));
}

TEST(JsonCodecTest, RejectsMalformedJson) {
    EXPECT_EQ(ui_error(std::string{R"({"type":"hello")"}).code, DecodeErrorCode::kMalformedJson);
    EXPECT_EQ(ui_error(std::string{""}).code, DecodeErrorCode::kMalformedJson);
    // 2 つの JSON の値を続けたもの。
    EXPECT_EQ(ui_error(std::string{R"({"type":"hello"}{})"}).code, DecodeErrorCode::kMalformedJson);
}

TEST(JsonCodecTest, RejectsInvalidUtf8) {
    const std::string text =
        "{\"type\":\"join\",\"request_id\":\"r1\",\"entity_id\":\"client\","
        "\"expected_revision\":1,\"invite\":\"\xff\xfe\"}";
    EXPECT_EQ(ui_error(text).code, DecodeErrorCode::kMalformedJson);
}

TEST(JsonCodecTest, RejectsMessageLargerThanLimit) {
    auto message = valid_mute();
    message["padding"] = std::string(kMaxMessageBytes, 'x');
    EXPECT_EQ(ui_error(message).code, DecodeErrorCode::kTooLarge);
}

TEST(JsonCodecTest, RejectsNonObjectMessage) {
    EXPECT_EQ(ui_error(std::string{R"(["hello"])"}).code, DecodeErrorCode::kNotObject);
    EXPECT_EQ(ui_error(std::string{R"("hello")"}).code, DecodeErrorCode::kNotObject);
    EXPECT_EQ(ui_error(std::string{"null"}).code, DecodeErrorCode::kNotObject);
}

TEST(JsonCodecTest, RejectsDeeplyNestedInputWithoutCrashing) {
    const std::string text = std::string(10000, '[') + std::string(10000, ']');
    EXPECT_EQ(ui_error(text).code, DecodeErrorCode::kNotObject);
}

TEST(JsonCodecTest, RejectsUnknownOrMissingType) {
    auto unknown = valid_mute();
    unknown["type"] = "format_disk";
    EXPECT_EQ(ui_error(unknown), (DecodeError{DecodeErrorCode::kUnknownType, "type"}));

    // エンジンから UI への種別は、UI からエンジンへのメッセージとしては未知。
    auto wrong_direction =
        nlohmann::json{{"type", "ack"}, {"request_id", "r1"}, {"applied_revision", 1}};
    EXPECT_EQ(ui_error(wrong_direction).code, DecodeErrorCode::kUnknownType);

    auto missing = valid_mute();
    missing.erase("type");
    EXPECT_EQ(ui_error(missing), (DecodeError{DecodeErrorCode::kMissingField, "type"}));

    auto not_string = valid_mute();
    not_string["type"] = 1;
    EXPECT_EQ(ui_error(not_string), (DecodeError{DecodeErrorCode::kWrongType, "type"}));

    // 大文字と小文字は区別する。
    auto upper = valid_mute();
    upper["type"] = "SET_CHANNEL_MUTE";
    EXPECT_EQ(ui_error(upper).code, DecodeErrorCode::kOutOfRange);
}

TEST(JsonCodecTest, RejectsMissingField) {
    for (const char* field : {"request_id", "entity_id", "expected_revision", "muted"}) {
        auto message = valid_mute();
        message.erase(field);
        EXPECT_EQ(ui_error(message), (DecodeError{DecodeErrorCode::kMissingField, field}));
    }
    // null を取りうるフィールドも省略はできない（UI の型では必須のフィールドなので）。
    auto gain = valid_gain();
    gain.erase("gain_db");
    EXPECT_EQ(ui_error(gain), (DecodeError{DecodeErrorCode::kMissingField, "gain_db"}));
}

TEST(JsonCodecTest, RejectsFieldOfWrongType) {
    auto muted = valid_mute();
    muted["muted"] = "true";
    EXPECT_EQ(ui_error(muted), (DecodeError{DecodeErrorCode::kWrongType, "muted"}));

    auto muted_number = valid_mute();
    muted_number["muted"] = 1;
    EXPECT_EQ(ui_error(muted_number).code, DecodeErrorCode::kWrongType);

    auto revision = valid_mute();
    revision["expected_revision"] = "3";
    EXPECT_EQ(ui_error(revision), (DecodeError{DecodeErrorCode::kWrongType, "expected_revision"}));

    auto fractional = valid_mute();
    fractional["expected_revision"] = 1.5;
    EXPECT_EQ(ui_error(fractional).code, DecodeErrorCode::kWrongType);

    auto entity = valid_mute();
    entity["entity_id"] = nullptr;
    EXPECT_EQ(ui_error(entity), (DecodeError{DecodeErrorCode::kWrongType, "entity_id"}));

    auto gain = valid_gain();
    gain["gain_db"] = "-6";
    EXPECT_EQ(ui_error(gain), (DecodeError{DecodeErrorCode::kWrongType, "gain_db"}));
}

TEST(JsonCodecTest, RejectsUnknownField) {
    auto message = valid_mute();
    message["force"] = true;
    EXPECT_EQ(ui_error(message), (DecodeError{DecodeErrorCode::kUnknownField, "force"}));

    // 種別に属さないフィールド（ミュートに gain_db）も未知として扱う。
    auto other_type_field = valid_mute();
    other_type_field["gain_db"] = 0.0;
    EXPECT_EQ(ui_error(other_type_field).code, DecodeErrorCode::kUnknownField);

    // 連続値のコマンドには expected_revision がない。
    auto gain = valid_gain();
    gain["expected_revision"] = 1;
    EXPECT_EQ(ui_error(gain).code, DecodeErrorCode::kUnknownField);
}

TEST(JsonCodecTest, RejectsCounterOutOfRange) {
    auto negative = valid_mute();
    negative["expected_revision"] = -1;
    EXPECT_EQ(ui_error(negative), (DecodeError{DecodeErrorCode::kOutOfRange, "expected_revision"}));

    auto too_large = valid_gain();
    too_large["seq"] = kMaxCounter + 1;
    EXPECT_EQ(ui_error(too_large), (DecodeError{DecodeErrorCode::kOutOfRange, "seq"}));

    auto max = valid_gain();
    max["seq"] = kMaxCounter;
    EXPECT_TRUE(decode_ui_to_engine(max.dump()));
}

TEST(JsonCodecTest, RejectsGainOutOfRange) {
    for (const double db : {kMaxGainDb + 0.01, kMinFiniteGainDb - 0.01, 1e308, -1e308}) {
        auto message = valid_gain();
        message["gain_db"] = db;
        EXPECT_EQ(ui_error(message), (DecodeError{DecodeErrorCode::kOutOfRange, "gain_db"})) << db;
    }
    // 1e400 は double で表せず、パーサーが無限大として読むので、範囲外として拒否する。
    const std::string overflow = R"({"type":"set_gain","entity_id":"ch1","seq":1,"gain_db":1e400})";
    const auto result = decode_ui_to_engine(overflow);
    EXPECT_FALSE(result);
}

TEST(JsonCodecTest, AcceptsGainAtBoundariesAndAsInteger) {
    for (const auto& db : {nlohmann::json(kMinFiniteGainDb), nlohmann::json(kMaxGainDb),
                           nlohmann::json(-6), nlohmann::json(0)}) {
        auto message = valid_gain();
        message["gain_db"] = db;
        const auto result = decode_ui_to_engine(message.dump());
        ASSERT_TRUE(result) << db;
        EXPECT_EQ(std::get<SetGain>(result.value()).gain_db, db.get<double>());
    }
}

TEST(JsonCodecTest, RejectsInvalidIdentifiers) {
    for (const std::string& id : std::vector<std::string>{"", std::string(65, 'a'), "ch 1", "ch1\n",
                                                          "チャンネル", "ch/1"}) {
        auto message = valid_mute();
        message["entity_id"] = id;
        EXPECT_EQ(ui_error(message), (DecodeError{DecodeErrorCode::kOutOfRange, "entity_id"}))
            << id;
    }
    auto request = valid_mute();
    request["request_id"] = "";
    EXPECT_EQ(ui_error(request), (DecodeError{DecodeErrorCode::kOutOfRange, "request_id"}));

    // 英数字と _ - . : は使える。64 バイトちょうども使える。
    for (const std::string& id : std::vector<std::string>{"ch-1_a.b:c", std::string(64, 'a')}) {
        auto message = valid_mute();
        message["entity_id"] = id;
        EXPECT_TRUE(decode_ui_to_engine(message.dump())) << id;
    }
}

TEST(JsonCodecTest, RejectsInviteAndDeviceIdOutOfRange) {
    const nlohmann::json join = {{"type", "join"},
                                 {"request_id", "r1"},
                                 {"entity_id", "client"},
                                 {"expected_revision", 1},
                                 {"invite", ""}};
    EXPECT_EQ(ui_error(join), (DecodeError{DecodeErrorCode::kOutOfRange, "invite"}));
    auto long_invite = join;
    long_invite["invite"] = std::string(2049, 'a');
    EXPECT_EQ(ui_error(long_invite).code, DecodeErrorCode::kOutOfRange);

    const nlohmann::json select = {{"type", "select_input"},
                                   {"request_id", "r1"},
                                   {"entity_id", "client"},
                                   {"expected_revision", 1},
                                   {"device_id", ""}};
    EXPECT_EQ(ui_error(select), (DecodeError{DecodeErrorCode::kOutOfRange, "device_id"}));
}

TEST(JsonCodecTest, RejectsInvalidEngineMessages) {
    // ストリップの状態に PENDING は来ない。入れ子のフィールドはドットでつないで示す。
    nlohmann::json channel = {{"entity_id", "ch1"},      {"revision", 1},
                              {"participant_id", "p1"},  {"name", "Vo"},
                              {"short_id", "#01"},       {"connection_state", "PENDING"},
                              {"signal_state", nullptr}, {"send_muted", true},
                              {"channel_muted", true},   {"gain_db", 0.0},
                              {"error_code", nullptr},   {"source_kind", "MICROPHONE"},
                              {"channel_count", 1},      {"source_number", 1}};
    EXPECT_EQ(engine_error({{"type", "channel_updated"}, {"channel", channel}}),
              (DecodeError{DecodeErrorCode::kOutOfRange, "channel.connection_state"}));

    // 入力のチャンネル数はモノラルかステレオだけ。
    channel["connection_state"] = "CONNECTED";
    channel["channel_count"] = 3;
    EXPECT_EQ(engine_error({{"type", "channel_updated"}, {"channel", channel}}),
              (DecodeError{DecodeErrorCode::kOutOfRange, "channel.channel_count"}));
    channel["channel_count"] = 1;
    channel["source_number"] = 0;
    EXPECT_EQ(engine_error({{"type", "channel_updated"}, {"channel", channel}}),
              (DecodeError{DecodeErrorCode::kOutOfRange, "channel.source_number"}));
    channel["source_number"] = 1;

    channel["connection_state"] = "CONNECTED";
    channel["signal_state"] = "LIVE";  // v1.1 以前の旧名称は受け付けない
    EXPECT_EQ(engine_error({{"type", "channel_updated"}, {"channel", channel}}),
              (DecodeError{DecodeErrorCode::kOutOfRange, "channel.signal_state"}));

    channel["signal_state"] = "RECEIVING";
    channel["volume"] = 1;
    EXPECT_EQ(engine_error({{"type", "channel_updated"}, {"channel", channel}}),
              (DecodeError{DecodeErrorCode::kUnknownField, "channel.volume"}));

    EXPECT_EQ(engine_error({{"type", "meters"}, {"levels", {{"ch1", "loud"}}}}),
              (DecodeError{DecodeErrorCode::kWrongType, "levels"}));
    EXPECT_EQ(engine_error({{"type", "meters"}, {"levels", nlohmann::json::array()}}),
              (DecodeError{DecodeErrorCode::kWrongType, "levels"}));
    EXPECT_EQ(engine_error({{"type", "notice"},
                            {"severity", "info"},
                            {"code", "join_request"},
                            {"params", {{"name", 1}}}}),
              (DecodeError{DecodeErrorCode::kWrongType, "params"}));
    EXPECT_EQ(engine_error({{"type", "notice"},
                            {"severity", "info"},
                            {"code", "Join Request"},
                            {"params", nlohmann::json::object()}}),
              (DecodeError{DecodeErrorCode::kOutOfRange, "code"}));
    EXPECT_EQ(engine_error({{"type", "pending_updated"}, {"pending", {1}}}),
              (DecodeError{DecodeErrorCode::kNotObject, "pending"}));
    EXPECT_EQ(
        engine_error({{"type", "set_gain"}, {"entity_id", "ch1"}, {"seq", 1}, {"gain_db", 0}}),
        (DecodeError{DecodeErrorCode::kUnknownType, "type"}));
}

/// 正しい stage_routing。テストごとに config の 1 か所だけ壊して使う。
nlohmann::json valid_stage_routing() {
    return {
        {"type", "stage_routing"},
        {"request_id", "r1"},
        {"entity_id", "routing"},
        {"expected_revision", 1},
        {"config",
         {{"buses", {{{"entity_id", "bus-cue"}, {"name", "返し"}, {"kind", "CUE"}}}},
          {"outputs", nlohmann::json::array()},
          {"sends",
           {{{"entity_id", "send-1"}, {"from", "ch1"}, {"to", "bus-cue"}, {"tap", "PRE_FADER"}}}},
          {"delays", {{{"entity_id", "delay-1"}, {"delay_ms", 100.0}}}},
          {"input_delays", {{{"strip_id", "ch1"}, {"delay_ms", 0}}}}}}};
}

TEST(JsonCodecTest, DecodesStageRouting) {
    const auto result = decode_ui_to_engine(valid_stage_routing().dump());
    ASSERT_TRUE(result) << result.error().field;
    const auto& command = std::get<DiscreteCommand>(result.value());
    const auto& config = std::get<StageRouting>(command.body).config;
    ASSERT_EQ(config.sends.size(), 1U);
    EXPECT_EQ(config.sends[0], (RoutingSend{"send-1", "ch1", "bus-cue", SendTap::kPreFader}));
    EXPECT_EQ(config.buses[0].kind, BusKind::kCue);
    EXPECT_EQ(config.delays[0].delay_ms, 100.0);
}

TEST(JsonCodecTest, RejectsInvalidRoutingConfig) {
    auto message = valid_stage_routing();
    message["config"]["delays"][0]["delay_ms"] = kMaxRoutingDelayMs + 1;
    EXPECT_EQ(ui_error(message),
              (DecodeError{DecodeErrorCode::kOutOfRange, "config.delays.delay_ms"}));

    message = valid_stage_routing();
    message["config"]["input_delays"][0]["delay_ms"] = -1;
    EXPECT_EQ(ui_error(message),
              (DecodeError{DecodeErrorCode::kOutOfRange, "config.input_delays.delay_ms"}));

    message = valid_stage_routing();
    message["config"]["sends"][0]["tap"] = "pre";
    EXPECT_EQ(ui_error(message), (DecodeError{DecodeErrorCode::kOutOfRange, "config.sends.tap"}));

    message = valid_stage_routing();
    message["config"]["sends"][0]["level"] = 0;
    EXPECT_EQ(ui_error(message),
              (DecodeError{DecodeErrorCode::kUnknownField, "config.sends.level"}));

    message = valid_stage_routing();
    message["config"]["buses"][0]["entity_id"] = "bus cue";
    EXPECT_EQ(ui_error(message),
              (DecodeError{DecodeErrorCode::kOutOfRange, "config.buses.entity_id"}));

    message = valid_stage_routing();
    message["config"].erase("outputs");
    EXPECT_EQ(ui_error(message), (DecodeError{DecodeErrorCode::kMissingField, "config.outputs"}));

    message = valid_stage_routing();
    message["config"]["outputs"] = {
        {{"entity_id", "out-1"}, {"name", "Mon"}, {"device_channel", -2}}};
    EXPECT_EQ(ui_error(message),
              (DecodeError{DecodeErrorCode::kOutOfRange, "config.outputs.device_channel"}));
}

TEST(JsonCodecTest, RoundTripsRoutingWithoutStagedConfig) {
    RoutingInfo routing;
    routing.entity_id = "routing";
    routing.revision = 2;
    routing.levels.push_back(RouteLevel{"bus-1", 1, std::nullopt, true});
    const EngineToUiMessage message = RoutingUpdated{routing};
    const auto json = nlohmann::json::parse(encode_engine_to_ui(message));
    EXPECT_TRUE(json["routing"]["staged"].is_null());
    EXPECT_TRUE(json["routing"]["levels"][0]["gain_db"].is_null());
    const auto decoded = decode_engine_to_ui(json.dump());
    ASSERT_TRUE(decoded);
    EXPECT_EQ(decoded.value(), message);
}

TEST(JsonCodecTest, RejectsInvalidRoutingCheck) {
    nlohmann::json routing = {
        {"entity_id", "routing"},
        {"revision", 1},
        {"applied",
         {{"buses", nlohmann::json::array()},
          {"outputs", nlohmann::json::array()},
          {"sends", nlohmann::json::array()},
          {"delays", nlohmann::json::array()},
          {"input_delays", nlohmann::json::array()}}},
        {"feedback_approved", false},
        {"levels", nlohmann::json::array()},
    };
    routing["staged"] = {{"config", routing["applied"]},
                         {"check",
                          {{"issue", "LOOP"},
                           {"issue_entity_id", nullptr},
                           {"looped_entity_ids", nlohmann::json::array()},
                           {"needs_approval", false}}}};
    EXPECT_EQ(engine_error({{"type", "routing_updated"}, {"routing", routing}}),
              (DecodeError{DecodeErrorCode::kOutOfRange, "routing.staged.check.issue"}));

    routing["staged"]["check"]["issue"] = nullptr;
    routing["staged"]["check"]["looped_entity_ids"] = {1};
    EXPECT_EQ(engine_error({{"type", "routing_updated"}, {"routing", routing}}),
              (DecodeError{DecodeErrorCode::kWrongType, "routing.staged.check.looped_entity_ids"}));
}

TEST(JsonCodecTest, EncodesNegativeInfinityAndNonFiniteValuesAsNull) {
    const auto gain = nlohmann::json::parse(encode_ui_to_engine(SetGain{"ch1", 2, std::nullopt}));
    EXPECT_TRUE(gain.at("gain_db").is_null());

    const Meters meters{{{"ch1", std::numeric_limits<double>::quiet_NaN()},
                         {"ch2", -std::numeric_limits<double>::infinity()},
                         {"ch3", std::nullopt}}};
    const auto encoded = nlohmann::json::parse(encode_engine_to_ui(meters));
    for (const char* id : {"ch1", "ch2", "ch3"}) {
        EXPECT_TRUE(encoded.at("levels").at(id).is_null()) << id;
    }
}

TEST(JsonCodecTest, EncodesInvalidUtf8TextWithReplacementCharacter) {
    // ネットワーク由来の参加者名が UTF-8 として不正でも、例外を出さずにエンコードする。
    PendingUpdated event;
    event.pending.push_back(PendingParticipant{"pending1", 1, "Vo\xff", "#01"});
    const auto text = encode_engine_to_ui(event);
    const auto decoded = decode_engine_to_ui(text);
    ASSERT_TRUE(decoded);
    EXPECT_EQ(std::get<PendingUpdated>(decoded.value()).pending.at(0).name, "Vo\xEF\xBF\xBD");
}

TEST(JsonCodecTest, EncodesDiscreteCommandWithBodyType) {
    const auto encoded = nlohmann::json::parse(
        encode_ui_to_engine(DiscreteCommand{"r9", "client", 4, SelectInput{std::nullopt}}));
    EXPECT_EQ(encoded, (nlohmann::json{{"type", "select_input"},
                                       {"request_id", "r9"},
                                       {"entity_id", "client"},
                                       {"expected_revision", 4},
                                       {"device_id", nullptr}}));
}

}  // namespace
}  // namespace vocroz::control_protocol
