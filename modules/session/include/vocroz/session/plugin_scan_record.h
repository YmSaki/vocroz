// SPDX-License-Identifier: AGPL-3.0-only
//
// プラグインファイルごとのスキャンの記録。
// 変更のないファイルを再スキャンしないためのキャッシュと、読み込み不可の記録を、同じ値で表す。
// 読み込み不可は、スキャン中に落ちたり止まったりしたファイルを以降のスキャンから外すための記録。
// 規則は docs/adr/0032-plugin-catalog-and-loader.md で決めた。
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "vocroz/session/plugin_description.h"

namespace vocroz::session {

/// プラグインファイルと、変更の有無を判定するための値。
/// 更新日時だけでは、日時を保ったままコピーで置き換えたファイルを見逃すので、サイズも比べる。
struct PluginFile {
    std::string path;  ///< ファイルのパス（UTF-8）。VST3 のバンドルならバンドルのディレクトリ
    /// 更新日時。値の単位と基準はスキャナーの実装が決め、等しいかどうかだけを比べる。
    std::int64_t modified_time = 0;
    std::uint64_t size_bytes = 0;  ///< サイズ（バンドルなら中のファイルの合計）

    friend bool operator==(const PluginFile&, const PluginFile&) = default;
};

/// スキャンの失敗の理由。
enum class PluginScanFailure {
    kCrashed,   ///< スキャン中にヘルパーが異常終了した（不良プラグイン）
    kTimedOut,  ///< 制限時間内に終わらなかった（不良プラグイン）
    kInvalidOutput,  ///< ヘルパーの出力がスキャン結果のスキーマに合わない
    /// ヘルパーを起動できなかった。プラグインではなくインストールの問題なので、
    /// そのファイルを読み込み不可として記録せず、スキャンを中断する。
    kScannerUnavailable,
};

/// スキャンの結果の区分。
enum class PluginScanStatus {
    kScanned,   ///< スキャンできた（プラグインを含まないファイルも含む）
    kCrashed,   ///< 読み込み不可: スキャン中に落ちた
    kTimedOut,  ///< 読み込み不可: スキャン中に止まった
    kInvalidOutput,  ///< 読み込み不可: 結果を読めなかった
};

/// プラグインファイル 1 つのスキャンの記録。
struct PluginScanRecord {
    PluginFile file;  ///< スキャンしたときのファイル
    PluginScanStatus status = PluginScanStatus::kScanned;
    /// 見つけたプラグイン。status が kScanned でなければ空。
    std::vector<PluginDescription> plugins;

    friend bool operator==(const PluginScanRecord&, const PluginScanRecord&) = default;
};

}  // namespace vocroz::session
