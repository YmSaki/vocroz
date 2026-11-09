// SPDX-License-Identifier: AGPL-3.0-only
//
// pa-engine と pa-plugin-scanner の間のメッセージ（スキャン結果）のスキーマ。
// ヘルパーはプラグインファイル 1 つをスキャンし、結果をこの形の JSON で標準出力に書く。
// 両側で同じコードを使い、スキーマを 1 か所に定義する（AGENTS.md 4.4）。
// ヘルパーの出力は不良プラグインに壊されうるので、pa-engine 側は信頼境界として検証してから使う。
#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "vocroz/common/result.h"

namespace vocroz::plugin_host {

/// スキャンで見つけたプラグイン 1 つの記述。
/// 1 つのファイルが複数のプラグインを含むことがある（シェルプラグイン）。
struct PluginDescription {
    std::string name;       ///< 表示名
    std::string vendor;     ///< 製造元
    std::string version;    ///< プラグインのバージョン（表示用）
    std::string file_path;  ///< プラグインファイルのパス（UTF-8）
    /// 読み込むときにファイル内のプラグインを特定する識別子。
    /// 同じファイルの中のプラグインを区別し、保存したセッションから同じプラグインを開き直すために使う。
    std::string identifier;
    bool is_instrument = false;      ///< VSTi（音声入力を持たず MIDI で発音する）か
    std::size_t input_channels = 0;  ///< 既定のメインバスの入力チャンネル数
    std::size_t output_channels = 0;  ///< 既定のメインバスの出力チャンネル数

    friend bool operator==(const PluginDescription&, const PluginDescription&) = default;
};

/// プラグインファイル 1 つのスキャン結果。
struct ScanReport {
    /// 見つけたプラグイン。プラグインでないファイルなら空。
    std::vector<PluginDescription> plugins;

    friend bool operator==(const ScanReport&, const ScanReport&) = default;
};

/// スキャン結果の JSON が不正な理由。
enum class ScanMessageError {
    kMalformedJson,  ///< JSON として読めない
    kUnsupportedVersion,  ///< スキーマのバージョンが違う（pa-engine とヘルパーの版の食い違い）
    kMissingField,  ///< 必須のフィールドがないか、型が違う
    kInvalidValue,  ///< 値が範囲外（文字列が長すぎる、チャンネル数が多すぎる、件数が多すぎる）
};

/// スキャン結果を JSON の文字列にする。
/// @param report スキャン結果
/// @return 1 行の JSON（UTF-8）。末尾に改行を含まない
/// スレッド安全性: 純粋関数。どのスレッドから呼んでもよい。
[[nodiscard]] std::string serialize_scan_report(const ScanReport& report);

/// ヘルパーの出力を検証してスキャン結果にする。
/// @param text ヘルパーが標準出力に書いた文字列
/// @return スキャン結果。不正なら ScanMessageError
/// スレッド安全性: 純粋関数。どのスレッドから呼んでもよい。
[[nodiscard]] common::Result<ScanReport, ScanMessageError> parse_scan_report(std::string_view text);

}  // namespace vocroz::plugin_host
