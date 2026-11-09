// SPDX-License-Identifier: AGPL-3.0-only
//
// JUCE の PluginDescription と、スキャン結果の PluginDescription（scan_messages.h）の変換。
// スキャン（記述を作る側）と読み込み（記述からプラグインを探す側）が、識別子の作り方を同じ知識として
// 使うので 1 か所に置く。
#pragma once

#include <juce_audio_processors_headless/juce_audio_processors_headless.h>

#include "vocroz/plugin_host/scan_messages.h"

namespace vocroz::plugin_host {

/// JUCE の記述からスキャン結果の記述を作る。
/// @param description JUCE の記述（VST3PluginFormatHeadless::findAllTypesForFile() の結果）
/// スレッド安全性: 純粋関数。どのスレッドから呼んでもよい。
[[nodiscard]] PluginDescription to_plugin_description(const juce::PluginDescription& description);

/// JUCE の記述が、スキャン結果の記述と同じプラグインを指すかを返す。
/// ファイルのパスではなく識別子で比べる。識別子には JUCE の createIdentifierString()
/// （形式、ファイル、プラグインの ID から作る）を使う。
/// @param candidate JUCE の記述
/// @param description スキャン結果の記述
/// スレッド安全性: 純粋関数。どのスレッドから呼んでもよい。
[[nodiscard]] bool describes_same_plugin(const juce::PluginDescription& candidate,
                                         const PluginDescription& description);

}  // namespace vocroz::plugin_host
