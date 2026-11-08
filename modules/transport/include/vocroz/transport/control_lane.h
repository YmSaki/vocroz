// SPDX-License-Identifier: AGPL-3.0-only
//
// 信頼制御層のレーン。レーンごとに到達と順序を保証し、レーンの間では順序を共有しない。
// MIDI を他の制御メッセージの再送待ちで遅らせないよう、MIDI は専用のレーンで送る
// （docs/adr/0009-beta-module-layout.md、docs/adr/0019-reliable-control-layer.md）。
#pragma once

#include <cstddef>
#include <cstdint>

#include "vocroz/transport/secure_channel.h"

namespace vocroz::transport {

/// 信頼制御層のレーン。値はワイヤー上の 1 バイトにそのまま載るので、既存の値は変えない。
enum class ControlLane : std::uint8_t {
    /// 承認、ミュート通知、入力なし通知などの制御メッセージ。
    kControl = 0,
    /// 子機からの MIDI イベント。
    kMidi = 1,
};

/// レーンの数。ControlLane の値は 0 から kControlLaneCount - 1 まで連続している。
inline constexpr std::size_t kControlLaneCount = 2;

/// ACK フレームの長さ（バイト）。種別（1）、レーン（1）、累積 ACK（4）、選択 ACK のビット列（8）。
inline constexpr std::size_t kAckFrameBytes = 14;

/// データフレームのヘッダーの長さ（バイト）。種別（1）、レーン（1）、連番（4）、長さ（2）。
inline constexpr std::size_t kDataFrameHeaderBytes = 8;

/// 制御メッセージ 1 つの最大長（バイト）。
/// 全レーンの ACK と一緒に 1 つのパケットへ必ず収まる長さにして、分割と再構成を持たずに済ませる。
inline constexpr std::size_t kMaxControlMessageBytes =
    kMaxPlaintextBytes - kDataFrameHeaderBytes - kControlLaneCount * kAckFrameBytes;

}  // namespace vocroz::transport
