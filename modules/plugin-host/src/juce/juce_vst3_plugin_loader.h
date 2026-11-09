// SPDX-License-Identifier: AGPL-3.0-only
//
// JUCE の VST3 のホストで、session の PluginLoader ポートを実装する（信頼済みインプロセス）。
// アウトオブプロセス実行は、別の PluginLoader の実装として並べる。
// その方針は docs/adr/0009-beta-module-layout.md で決めた。
#pragma once

#include <memory>

#include "vocroz/session/ports/plugin_loader.h"

namespace vocroz::plugin_host {

/// VST3 のプラグインを、このプロセスの中に読み込む。
/// スレッド安全性: JUCE のメッセージスレッドから呼ぶ。
class JuceVst3PluginLoader final : public session::PluginLoader {
public:
    [[nodiscard]] common::Result<std::unique_ptr<session::PluginInstance>, session::PluginLoadError>
    load(const session::PluginDescription& description) override;
};

}  // namespace vocroz::plugin_host
