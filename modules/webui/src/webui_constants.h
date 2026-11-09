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

/// UI からネイティブ側へ、窓の操作を求めるイベントの ID。ui/src/ipc/juce-window.ts の
/// kWindowEventId と同じ値。値は kOpenRoutingRequest か kCloseWindowRequest。
inline constexpr std::string_view kWindowEventId = "vocroz.window";
inline constexpr std::string_view kOpenRoutingRequest = "open_routing";
inline constexpr std::string_view kCloseWindowRequest = "close";

/// 初期化データの名前。ui/src/ipc/launch-config.ts が読む名前と同じ。
inline constexpr std::string_view kInitModeKey = "mode";
inline constexpr std::string_view kInitLangKey = "lang";
inline constexpr std::string_view kInitJoinKey = "join";
inline constexpr std::string_view kInitScreenKey = "screen";
inline constexpr std::string_view kInitRequestPrefixKey = "request_prefix";

}  // namespace vocroz::webui
