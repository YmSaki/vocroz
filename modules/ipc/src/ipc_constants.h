// SPDX-License-Identifier: AGPL-3.0-only
//
// ipc モジュールの中だけで使う定数。
#pragma once

#include <chrono>
#include <cstddef>
#include <string_view>

namespace vocroz::ipc {

/// pa-engine のパイプの名前の前半。後ろに SID とデータフォルダのハッシュが続く。
inline constexpr std::string_view kEnginePipePrefix = R"(\\.\pipe\vocroz-engine-)";

/// 1 接続の送信キューに積める未送信のバイト数の上限。
/// 超えたら相手が受信していないとみなして接続を切る（docs/adr/0020-ipc-pipe-and-webui-bridge.md）。
/// 最大のメッセージ（64 KiB）を 16 個積める大きさで、通常の送信では届かない。
/// UI がハングしてメーター（30 Hz、1 件 1 KiB 程度）を読まなくなると、数十秒以内に達する。
inline constexpr std::size_t kMaxQueuedBytes = std::size_t{1024} * 1024;

/// パイプのカーネル側のバッファの大きさ（送信と受信のそれぞれ）。
/// 最大のメッセージ 1
/// つとその長さの前置きが収まる大きさにし、通常は書き込みが待たされないようにする。
inline constexpr std::size_t kPipeBufferBytes = std::size_t{64} * 1024 + 4;

/// 1 回の ReadFile で読むバイト数。
inline constexpr std::size_t kReadChunkBytes = std::size_t{64} * 1024;

/// パイプがまだない（エンジンの起動中）ときに、作成を待って再試行する間隔。
/// エンジンの起動は数百ミリ秒で終わる見込みで、接続の遅れが体感できない程度に短くする。
inline constexpr std::chrono::milliseconds kConnectRetryInterval{50};

}  // namespace vocroz::ipc
