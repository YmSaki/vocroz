// SPDX-License-Identifier: AGPL-3.0-only
//
// 読み込んだ JUCE のプラグインを、session の PluginInstance ポートとして扱うアダプター。
// 処理（グラフのノード）と状態の保存と復元を、同じ AudioPluginInstance で行う。
#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "vocroz/session/ports/plugin_loader.h"

namespace vocroz::plugin_host {

class JucePluginProcessor;

/// 読み込んだプラグイン 1 つ。
/// スレッド安全性: すべて JUCE のメッセージスレッドから呼ぶ。破棄もそのスレッドで行う。
class JucePluginInstance final : public session::PluginInstance {
public:
    /// @param processor 準備済みのプラグインの処理（null でないこと）
    explicit JucePluginInstance(std::shared_ptr<JucePluginProcessor> processor);

    [[nodiscard]] std::shared_ptr<audio_core::PluginProcessor> processor() const override;
    [[nodiscard]] common::Result<std::vector<std::uint8_t>, session::PluginStateError> save_state()
        override;
    [[nodiscard]] common::Result<void, session::PluginStateError> restore_state(
        std::span<const std::uint8_t> state) override;

private:
    std::shared_ptr<JucePluginProcessor> processor_;
};

}  // namespace vocroz::plugin_host
