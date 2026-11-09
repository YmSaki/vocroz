// SPDX-License-Identifier: AGPL-3.0-only
//
// スキャン結果のスキーマの単体テスト。往復で値が保たれることと、不良ヘルパーの出力
// （壊れた JSON、版の違い、欠けたフィールド、範囲外の値）を拒否することを検証する。

#include "vocroz/plugin_host/scan_messages.h"

#include <string>

#include <gtest/gtest.h>

namespace vocroz::plugin_host {
namespace {

PluginDescription sample_plugin() {
    PluginDescription plugin;
    plugin.name = "ルームリバーブ";
    plugin.vendor = "Example Audio";
    plugin.version = "1.2.3";
    plugin.file_path = "C:/Program Files/Common Files/VST3/Room Reverb.vst3";
    plugin.identifier = "VST3-Room Reverb-1a2b3c4d-5e6f7a8b";
    plugin.is_instrument = false;
    plugin.input_channels = 2;
    plugin.output_channels = 2;
    return plugin;
}

ScanMessageError parse_error(const std::string& text) {
    auto result = parse_scan_report(text);
    EXPECT_FALSE(result.has_value()) << text;
    return result.has_value() ? ScanMessageError::kMalformedJson : result.error();
}

TEST(ScanMessagesTest, RoundTripsReportWithMultiplePlugins) {
    ScanReport report;
    report.plugins.push_back(sample_plugin());
    PluginDescription synth = sample_plugin();
    synth.name = "Synth";
    synth.identifier = "VST3-Synth-0-1";
    synth.is_instrument = true;
    synth.input_channels = 0;
    report.plugins.push_back(synth);

    auto parsed = parse_scan_report(serialize_scan_report(report));
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed.value(), report);
}

TEST(ScanMessagesTest, RoundTripsEmptyReportForNonPluginFile) {
    const std::string text = serialize_scan_report({});
    EXPECT_EQ(text.find('\n'), std::string::npos);  // 1 行で書く
    auto parsed = parse_scan_report(text);
    ASSERT_TRUE(parsed.has_value());
    EXPECT_TRUE(parsed.value().plugins.empty());
}

TEST(ScanMessagesTest, RejectsMalformedOrTruncatedOutput) {
    EXPECT_EQ(parse_error(""), ScanMessageError::kMalformedJson);
    EXPECT_EQ(parse_error("{\"version\":1,\"plugins\":["), ScanMessageError::kMalformedJson);
    EXPECT_EQ(parse_error("[]"), ScanMessageError::kMalformedJson);
}

TEST(ScanMessagesTest, RejectsOtherSchemaVersion) {
    EXPECT_EQ(parse_error(R"({"version":2,"plugins":[]})"), ScanMessageError::kUnsupportedVersion);
    EXPECT_EQ(parse_error(R"({"plugins":[]})"), ScanMessageError::kMissingField);
}

TEST(ScanMessagesTest, RejectsMissingOrMistypedFields) {
    EXPECT_EQ(parse_error(R"({"version":1})"), ScanMessageError::kMissingField);
    EXPECT_EQ(parse_error(R"({"version":1,"plugins":[{"name":"x"}]})"),
              ScanMessageError::kMissingField);
    EXPECT_EQ(parse_error(R"({"version":1,"plugins":[1]})"), ScanMessageError::kMissingField);

    std::string text = serialize_scan_report({{sample_plugin()}});
    const auto at = text.find("\"input_channels\":2");
    text.replace(at, 18, "\"input_channels\":-1");
    EXPECT_EQ(parse_error(text), ScanMessageError::kMissingField);
}

TEST(ScanMessagesTest, RejectsOutOfRangeValues) {
    PluginDescription many_channels = sample_plugin();
    many_channels.output_channels = 129;
    EXPECT_EQ(parse_error(serialize_scan_report({{many_channels}})),
              ScanMessageError::kInvalidValue);

    PluginDescription no_identifier = sample_plugin();
    no_identifier.identifier.clear();
    EXPECT_EQ(parse_error(serialize_scan_report({{no_identifier}})),
              ScanMessageError::kInvalidValue);

    PluginDescription long_name = sample_plugin();
    long_name.name.assign(98'305, 'a');
    EXPECT_EQ(parse_error(serialize_scan_report({{long_name}})), ScanMessageError::kInvalidValue);
}

TEST(ScanMessagesTest, ReplacesInvalidUtf8InsteadOfFailing) {
    PluginDescription broken = sample_plugin();
    broken.name = std::string("bad\xff");
    auto parsed = parse_scan_report(serialize_scan_report({{broken}}));
    ASSERT_TRUE(parsed.has_value());
    EXPECT_NE(parsed.value().plugins[0].name, broken.name);
}

}  // namespace
}  // namespace vocroz::plugin_host
