// SPDX-License-Identifier: AGPL-3.0-only
//
// スキャンで見つけたプラグインの記述。
// session がプラグインの一覧と挿入を扱い、plugin-host がスキャンと読み込みで同じ型を使うので、
// 内側の session に置く（docs/adr/0032-plugin-catalog-and-loader.md）。
#pragma once

#include <cstddef>
#include <string>

namespace vocroz::session {

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

}  // namespace vocroz::session
