// SPDX-License-Identifier: AGPL-3.0-only
//
// スキャンの記録の JSON の変換のテスト。往復と、壊れた記録を読まないことを確かめる。

#include "plugin_catalog_codec.h"

#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace vocroz::persistence {
namespace {

std::vector<session::PluginScanRecord> sample() {
    session::PluginDescription eq;
    eq.name = "Pro EQ";
    eq.vendor = "例の会社";
    eq.version = "1.2.0";
    eq.file_path = "C:/Program Files/Common Files/VST3/EQ.vst3";
    eq.identifier = "VST3-Pro EQ-1a2b-3c4d";
    eq.input_channels = 2;
    eq.output_channels = 2;
    session::PluginDescription synth = eq;
    synth.name = "Synth";
    synth.identifier = "VST3-Synth-5e6f-7a8b";
    synth.is_instrument = true;
    synth.input_channels = 0;
    session::PluginScanRecord scanned{
        session::PluginFile{eq.file_path, 133'000'000'000'000'000, 4096},
        session::PluginScanStatus::kScanned,
        {eq, synth}};
    session::PluginScanRecord crashed{
        session::PluginFile{"C:/VST3/Bad.vst3", -5, 10}, session::PluginScanStatus::kCrashed, {}};
    return {scanned, crashed};
}

TEST(PluginCatalogCodecTest, RoundTripsRecords) {
    const auto records = sample();
    const auto decoded = decode_plugin_catalog(encode_plugin_catalog(records));
    ASSERT_TRUE(decoded);
    EXPECT_EQ(*decoded, records);
}

TEST(PluginCatalogCodecTest, RoundTripsAnEmptyCatalog) {
    const auto decoded = decode_plugin_catalog(encode_plugin_catalog({}));
    ASSERT_TRUE(decoded);
    EXPECT_TRUE(decoded->empty());
}

TEST(PluginCatalogCodecTest, RejectsBrokenOrForeignText) {
    EXPECT_FALSE(decode_plugin_catalog(""));
    EXPECT_FALSE(decode_plugin_catalog("{"));
    EXPECT_FALSE(decode_plugin_catalog(R"({"schema_version":2,"records":[]})"));
    EXPECT_FALSE(decode_plugin_catalog(R"({"schema_version":1})"));
    // 知らない状態
    EXPECT_FALSE(decode_plugin_catalog(
        R"({"schema_version":1,"records":[{"path":"a","modified_time":1,"size_bytes":1,"status":"lost","plugins":[]}]})"));
    // 負のサイズ
    EXPECT_FALSE(decode_plugin_catalog(
        R"({"schema_version":1,"records":[{"path":"a","modified_time":1,"size_bytes":-1,"status":"scanned","plugins":[]}]})"));
    // プラグインの識別子がない
    EXPECT_FALSE(decode_plugin_catalog(
        R"({"schema_version":1,"records":[{"path":"a","modified_time":1,"size_bytes":1,"status":"scanned","plugins":[{"name":"x","vendor":"","version":"","file_path":"a","is_instrument":false,"input_channels":2,"output_channels":2}]}]})"));
}

}  // namespace
}  // namespace vocroz::persistence
