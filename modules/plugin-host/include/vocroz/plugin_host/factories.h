// SPDX-License-Identifier: AGPL-3.0-only
//
// plugin-host のアダプターを作る関数。JUCE を使うので、VOCROZ_WITH_JUCE が ON のときだけある。
// 具象クラスは公開せず、ここの関数だけを公開する（AGENTS.md 5.1）。
// エディターのウィンドウ（session の PluginEditorHost）は、作るときにここへ加える。
#pragma once

#include <chrono>
#include <filesystem>
#include <memory>
#include <vector>

#include "vocroz/common/result.h"
#include "vocroz/plugin_host/scan_messages.h"
#include "vocroz/session/ports/plugin_loader.h"
#include "vocroz/session/ports/plugin_scanner.h"

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

/// スキャン用ヘルパーでスキャンする PluginScanner を作る。
/// directories の下の .vst3 のファイルとバンドルを列挙し、ファイルごとに scan_in_helper()
/// でスキャンする。
/// @param helper ヘルパーの実行ファイル（pa-plugin-scanner）
/// @param directories 探す場所
/// @param timeout ファイル 1 つの制限時間（既定値は constants.h の kDefaultScanTimeout）
/// @return スキャナー
/// スレッド安全性: 返したスキャナーは PluginCatalog と同じスレッドから使う。
[[nodiscard]] std::unique_ptr<session::PluginScanner> make_helper_plugin_scanner(
    std::filesystem::path helper, std::vector<std::filesystem::path> directories,
    std::chrono::milliseconds timeout);

/// VST3 のプラグインをこのプロセスの中に読み込む PluginLoader を作る（信頼済みインプロセス）。
/// 読み込んだプラグインは、エンジン内部のサンプルレートとブロック長で処理の準備を済ませる。
/// サイドチェインなどのメイン以外のバスは無効にし、エフェクトはステレオ入出力を、
/// それができなければモノラル入力とステレオ出力、モノラル入出力の順に選ぶ。
/// @return ローダー
/// スレッド安全性: 返したローダーは JUCE のメッセージスレッドから使う。
[[nodiscard]] std::unique_ptr<session::PluginLoader> make_vst3_plugin_loader();

}  // namespace vocroz::plugin_host
