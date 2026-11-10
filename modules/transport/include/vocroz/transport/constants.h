// SPDX-License-Identifier: AGPL-3.0-only
//
// transport の外でも使う定数。
// 受信バッファの大きさを決める network や、公開鍵を扱う session が、
// ワイヤーフォーマットと同じ値を使えるようにする。
// メディアの形式は、子機の送信側（capture と client-desktop）と親機の受信側が同じ値を使うので、
// ここで 1 か所に定義する。
#pragma once

#include <cstddef>
#include <cstdint>

namespace vocroz::transport {

/// UDP ペイロードとして送受信するパケットの最大長（バイト）。
/// IPv6 の最小 MTU（1280）から IPv6 ヘッダー（40）と UDP ヘッダー（8）を引いた値で、
/// どの経路でも IP フラグメンテーションを起こさない。フラグメントは 1 つ失うと
/// パケット全体が失われ、遅延と欠損が増えるため避ける。
inline constexpr std::size_t kMaxPacketBytes = 1232;

/// 親機から子機への返送（Minus-Me、下りストリーム）のストリーム ID。
/// ストリーム ID は向きごとに別の名前空間で、上り（子機が入力ごとに 0 から振る）とは衝突しない。
/// 返送は子機 1 台に 1 本なので 0 だけを使う（docs/adr/0036-return-monitor.md）。
inline constexpr std::uint16_t kReturnStreamId = 0;

/// Curve25519 の公開鍵の長さ（バイト）。
inline constexpr std::size_t kPublicKeyBytes = 32;

/// Curve25519 の秘密鍵の長さ（バイト）。
inline constexpr std::size_t kPrivateKeyBytes = 32;

/// メディアのサンプルレート（Hz）。
/// 内部フォーマットとメディアのタイムスタンプの時計は 48kHz に固定する。
/// 入力デバイスのレートが異なる場合は、デバイスの境界でリサンプリングしてから渡す。
inline constexpr std::uint32_t kSampleRate = 48000;

/// Opus の 1 フレームの長さ。
/// 合奏の低遅延のため 2.5ms か 5ms だけを使う（docs/adr/0002-opus-low-delay.md）。
enum class FrameDuration : std::uint8_t {
    k2500Us,  ///< 2.5ms（120 サンプル）
    k5000Us,  ///< 5ms（240 サンプル）
};

/// フレーム長に対応する 48kHz のサンプル数を返す。
/// @param duration フレーム長
/// @return 1 フレームのサンプル数
/// スレッド安全性: どのスレッドからも呼び出し可能。オーディオスレッドから呼び出し可能。
[[nodiscard]] constexpr std::size_t frame_samples(FrameDuration duration) noexcept {
    switch (duration) {
        case FrameDuration::k2500Us:
            return 120;
        case FrameDuration::k5000Us:
            return 240;
    }
    return 240;  // 列挙子以外の値は来ないが、未定義の値でも 1
                 // フレームとして扱えるよう長いほうを返す
}

/// 1 ストリームのチャンネル数の上限。
/// 子機の入力はモノラルかステレオだけを送る（docs/adr/0035-client-input-expansion.md）。
/// サラウンドは合奏の用途になく、Opus のマルチストリームの扱いを増やさないため。
inline constexpr std::size_t kMaxStreamChannels = 2;

/// ステレオのストリームの目標ビットレートの目安（bps）。
/// モノラルの既定値（128kbps）の 1.5 倍にする。Opus はステレオの左右の相関を使って符号化するので、
/// 2 倍にしなくても同じ音質を保てる。最終値はモノラルと同じく P0 の実測で決める。
inline constexpr std::int32_t kStereoBitrateBps = 192000;

/// 1 フレームの Opus ペイロードの上限（バイト）。
/// Opus の最大ビットレート 510kbps（ステレオでも同じ）で 5ms のフレームを作ると約 319
/// バイトになるので、
/// 余裕を見て 400 にする。
/// Opus の仕様上の上限（1275 バイト）より小さくするのは、フレームを固定長の配列で持ち、
/// ストリームごとのキューとジッターバッファの事前確保の量を抑えるため。
inline constexpr std::size_t kMaxOpusPayloadBytes = 400;

}  // namespace vocroz::transport
