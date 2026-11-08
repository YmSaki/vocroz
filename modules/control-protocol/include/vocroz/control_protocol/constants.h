// SPDX-License-Identifier: AGPL-3.0-only
//
// UI とエンジン間のメッセージの、どの entity にも共通する上限と範囲。
// 名前付きパイプ（ipc）と WebView のブリッジ（webui）のフレーミングも、同じ上限を使う。
#pragma once

#include <cstddef>
#include <cstdint>

namespace vocroz::control_protocol {

/// 1 メッセージの JSON の最大バイト数。
/// 最大のメッセージは親機のスナップショットで、ストリップ 64 本でも 20 KiB 程度に収まる。
/// 余裕を見て 64 KiB
/// とし、これを超える入力は不正として読まずに拒否する（巨大な入力でメモリを使わせないため）。
inline constexpr std::size_t kMaxMessageBytes = std::size_t{64} * 1024;

/// ゲインの有限の最小値（dB）。ミキサーのフェーダーと子機の送信レベルの下限はどちらも -60 dB で、
/// それより下のフェーダーは -∞ dB（JSON では null）になる。
inline constexpr double kMinFiniteGainDb = -60.0;

/// ゲインの最大値（dB）。ミキサーのフェーダーは +10 dB、子機の送信レベルは +12 dB までで、
/// 大きいほうに合わせる。entity ごとの上限は session が判断する。
inline constexpr double kMaxGainDb = 12.0;

/// revision とシーケンス番号の最大値。UI（JavaScript）の数値で正確に表せる最大の整数（2^53 - 1）。
inline constexpr std::uint64_t kMaxCounter = (std::uint64_t{1} << 53) - 1;

}  // namespace vocroz::control_protocol
