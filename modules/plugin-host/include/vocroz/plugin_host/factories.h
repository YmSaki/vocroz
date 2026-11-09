// SPDX-License-Identifier: AGPL-3.0-only
//
// plugin-host のアダプターを作る関数。JUCE を使うので、VOCROZ_WITH_JUCE が ON のときだけある。
// 具象クラスは公開せず、ここの関数だけを公開する（AGENTS.md 5.1）。
// session のポート（PluginLoader、PluginScanner、PluginEditorHost）ができたら、
// それを返すファクトリー関数をここに加える。
#pragma once

#include <chrono>
#include <filesystem>
#include <memory>
#include <string>

#include "vocroz/audio_core/ports/plugin_processor.h"
#include "vocroz/common/result.h"
#include "vocroz/plugin_host/scan_messages.h"

namespace vocroz::plugin_host {

/// ヘルパーでのスキャンが失敗した理由。どれも、そのファイルを読み込み不可として記録する理由になる。
enum class HelperScanError {
    kLaunchFailed,  ///< ヘルパーを起動できなかった（ヘルパーの実行ファイルがない、など）
    kCrashed,  ///< ヘルパーが異常終了した（不良プラグインがスキャン中に落ちた）
    kTimedOut,  ///< 制限時間内に終わらなかった（不良プラグインがスキャン中に止まった）
    kInvalidOutput,  ///< 出力がスキャン結果のスキーマに合わない
};

/// プラグインファイル 1 つを、別プロセスのヘルパー（pa-plugin-scanner）でスキャンする。
/// 不良プラグインがスキャン中に落ちても止まっても、呼び出し側のプロセスは巻き込まれない。
/// 制限時間を過ぎたら、ヘルパーを強制終了する。
/// @param helper ヘルパーの実行ファイル
/// @param plugin_file スキャンするプラグインファイル（VST3 のバンドルかファイル）
/// @param timeout 制限時間
/// @return スキャン結果。失敗なら HelperScanError
/// スレッド安全性: ブロックする。コントロールスレッドか専用のスレッドから呼ぶ。
/// オーディオスレッドからは呼ばない。
[[nodiscard]] common::Result<ScanReport, HelperScanError> scan_in_helper(
    const std::filesystem::path& helper, const std::filesystem::path& plugin_file,
    std::chrono::milliseconds timeout);

/// プラグインファイル 1 つを、このプロセスの中でスキャンする。
/// 不良プラグインはこのプロセスごと落とすので、pa-plugin-scanner の中でだけ呼ぶ。
/// @param plugin_file スキャンするプラグインファイル
/// @return スキャン結果。プラグインでないファイルなら空の結果
/// スレッド安全性: JUCE のメッセージスレッドから呼ぶ。
[[nodiscard]] ScanReport scan_in_process(const std::filesystem::path& plugin_file);

/// プラグインを読み込めなかった理由。
enum class PluginLoadError {
    kNotFound,  ///< ファイルがないか、記述の識別子に合うプラグインがファイルの中にない
    kInstantiateFailed,  ///< プラグインのインスタンスを作れなかった
    kUnsupportedLayout,  ///< 入力 0〜2、出力 1〜2 のチャンネル構成にできなかった
};

/// プラグインを読み込み、グラフのプラグインノードに渡せる形にする。
/// 処理の準備は kSampleRate と kBlockSize で済ませる。
/// サイドチェインなどのメイン以外のバスは無効にする。
/// エフェクトはステレオ入出力を、それができなければモノラル入出力を選ぶ。
/// @param description スキャン結果の記述
/// @return 準備済みのプラグイン。失敗なら PluginLoadError
/// スレッド安全性: JUCE のメッセージスレッドから呼ぶ。返したプラグインの最後の参照も、
/// JUCE のメッセージスレッド（pa-engine ではコントロールスレッド）で外す。
[[nodiscard]] common::Result<std::shared_ptr<audio_core::PluginProcessor>, PluginLoadError>
make_vst3_plugin_processor(const PluginDescription& description);

}  // namespace vocroz::plugin_host
