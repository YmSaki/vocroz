// SPDX-License-Identifier: AGPL-3.0-only
//
// webui モジュールの中だけで使う定数。
#pragma once

#include <string_view>

namespace vocroz::webui {

/// UI からネイティブ側へ送るイベントの ID。ui/src/ipc/juce-transport.ts の kToNativeEventId
/// と同じ値。
inline constexpr std::string_view kToEngineEventId = "vocroz.to_engine";

/// ネイティブ側から UI へ送るイベントの ID。ui/src/ipc/juce-transport.ts の kFromNativeEventId
/// と同じ値。
inline constexpr std::string_view kFromEngineEventId = "vocroz.from_engine";

/// 初期化データの名前。ui/src/ipc/launch-config.ts が読む名前と同じ。
inline constexpr std::string_view kInitModeKey = "mode";
inline constexpr std::string_view kInitLangKey = "lang";
inline constexpr std::string_view kInitJoinKey = "join";

}  // namespace vocroz::webui
