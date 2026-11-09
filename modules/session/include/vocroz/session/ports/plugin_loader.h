// SPDX-License-Identifier: AGPL-3.0-only
//
// プラグインを読み込み、状態を保存して復元するポート。plugin-host が JUCE で実装する。
// 読み込んだプラグインの処理は audio-core の PluginProcessor としてグラフのノードに渡し、
// 状態の保存と復元は session がこのポートの PluginInstance を通して指示する。
#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "vocroz/audio_core/ports/plugin_processor.h"
#include "vocroz/common/result.h"
#include "vocroz/session/plugin_description.h"

namespace vocroz::session {

/// プラグインを読み込めなかった理由。
enum class PluginLoadError {
    kNotFound,  ///< ファイルがないか、記述の識別子に合うプラグインがファイルの中にない
    kInstantiateFailed,  ///< プラグインのインスタンスを作れなかった
    kUnsupportedLayout,  ///< 入力 0〜2、出力 1〜2 のチャンネル構成にできなかった
};

/// プラグインの状態を保存または復元できなかった理由。
enum class PluginStateError {
    kRejected,  ///< プラグインが失敗した（例外を送出した、状態を受け付けなかった）
};

/// 読み込んだプラグイン 1 つ。
/// スレッド安全性: すべて JUCE のメッセージスレッド（pa-engine ではコントロールスレッド）から呼ぶ。
/// 破棄もそのスレッドで行う。
class PluginInstance {
public:
    virtual ~PluginInstance() = default;

    /// グラフのプラグインノードに渡す処理。ノードが持っている間は、このインスタンスより長く生きる。
    /// @return 処理（null にならない）
    [[nodiscard]] virtual std::shared_ptr<audio_core::PluginProcessor> processor() const = 0;

    /// プラグインの状態（パラメーターとプリセット）を、プラグイン独自の形式のバイト列にする。
    /// オーディオスレッドの処理と並行して呼んでよい（VST3 の取り決め）。
    /// @return 状態。プラグインが失敗したら理由
    [[nodiscard]] virtual common::Result<std::vector<std::uint8_t>, PluginStateError>
    save_state() = 0;

    /// save_state() で得た状態を戻す。
    /// @param state 同じプラグインの save_state() の結果
    /// @return 失敗したら理由。プラグインは失敗前の状態か、途中まで戻した状態のまま
    [[nodiscard]] virtual common::Result<void, PluginStateError> restore_state(
        std::span<const std::uint8_t> state) = 0;
};

/// プラグインの読み込み。
/// スレッド安全性: JUCE のメッセージスレッド（pa-engine ではコントロールスレッド）から呼ぶ。
class PluginLoader {
public:
    virtual ~PluginLoader() = default;

    /// スキャンで得た記述のプラグインを読み込み、処理の準備まで済ませる。
    /// @param description PluginCatalog の一覧にある記述
    /// @return 読み込んだプラグイン（null にならない）。失敗なら理由
    [[nodiscard]] virtual common::Result<std::unique_ptr<PluginInstance>, PluginLoadError> load(
        const PluginDescription& description) = 0;
};

}  // namespace vocroz::session
