// SPDX-License-Identifier: AGPL-3.0-only
//
// capture モジュールの中だけで使う定数。
#pragma once

#include <cstddef>
#include <cstdint>

namespace vocroz::capture {

/// アプリ音声のチャンネル数。再生デバイスのチャンネル数によらずステレオにそろえる。
/// 子機はアプリ音声をステレオの入力として申告する（session の kAppAudioChannels と同じ値）。
inline constexpr std::size_t kLoopbackChannels = 2;

/// ステレオのスピーカー配置（SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT）。
/// サラウンドの再生デバイスからは、OS の変換で左右にまとめた音を受け取る。
inline constexpr std::uint32_t kStereoChannelMask = 0x3;

/// デバイスのイベントを待つ時間の上限（ミリ秒）。
/// 再生デバイスが抜けるとイベントが届かなくなるので、この時間ごとにバッファの状態を問い合わせ、
/// AUDCLNT_E_DEVICE_INVALIDATED で抜去を検知する。device モジュールのマイク入力と同じ考え方の値。
inline constexpr std::uint32_t kStreamWatchdogMilliseconds = 100;

/// プロセスループバックの活性化（ActivateAudioInterfaceAsync）の完了を待つ時間の上限（ミリ秒）。
/// 完了は通常すぐに届く。届かないまま開始を止め続けると、アプリ音声を加える操作が戻らないので、
/// 上限を過ぎたら開けなかったとして扱う。操作者が待てる長さにする。
inline constexpr std::uint32_t kActivationTimeoutMilliseconds = 2'000;

/// ループバックのストリームのバッファ長（100ns 単位、20ms）。
/// プロセスループバックは実在のデバイスではなく、デバイスの既定値（ミックスフォーマットなど）を
/// 問い合わせられないので、Microsoft の ApplicationLoopback
/// のサンプルと同じ長さを明示する。イベント駆動で周期ごとに
/// 読み切るので、バッファ長は上限であって遅延にはならない。
inline constexpr std::int64_t kLoopbackBufferDuration = 200'000;

/// 無音を流し続ける再生ストリームのバッファ長（100ns 単位、100ms）。
/// 再生しているアプリがない間は、オーディオエンジンが止まってループバックにパケットが届かない。
/// 無音の再生ストリームを開いておくとエンジンが動き続け、無音のパケットが周期どおり届く。
/// 補充はループバックのイベントごと（10ms 程度）に行う。
/// それより十分長くして、途切れないようにする。
inline constexpr std::int64_t kKeepAliveBufferDuration = 1'000'000;

}  // namespace vocroz::capture
