// SPDX-License-Identifier: AGPL-3.0-only
//
// 子機モードの組み立てだけで使う定数。値の根拠はそれぞれのコメントに書く。
#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>

#include "vocroz/transport/constants.h"

namespace vocroz::client {

/// 1 フレームの最大サンプル数。transport の最長のフレーム（5ms）に合わせ、
/// オーディオスレッドの作業領域をこの長さで事前に確保する。
inline constexpr std::size_t kMaxFrameSamples =
    transport::frame_samples(transport::FrameDuration::k5000Us);

/// オーディオスレッドから送信スレッドへ渡す、エンコード済みフレームの数の上限。
/// 5ms フレームで 160ms 分。送信スレッドがこれより長く止まったら、古いものから捨てるのではなく
/// 新しいフレームを捨てる（SPSC キューの性質）。受信側からは欠損と同じに見える。
inline constexpr std::size_t kEncodedFrameQueueCapacity = 32;

/// コントロールスレッドから送信スレッドへ渡す、制御パケットの平文の数の上限。
/// 信頼制御層の窓（未 ACK のメッセージ数）より十分大きくし、通常の操作では溢れないようにする。
/// 溢れた平文は失われたものとして信頼制御層が再送する。
inline constexpr std::size_t kControlPlaintextQueueCapacity = 64;

/// ネットワーク受信スレッドからコントロールスレッドへ渡す、相手のパケットの数の上限。
/// 子機が親機から受け取るのは制御パケットだけ（下りの音声は Beta）で、毎秒数個しか届かない。
inline constexpr std::size_t kPeerInboxCapacity = 64;

/// 送信ゲインと送信ミュートのランプの長さ。仕様のゲインランプ（5〜20ms）の短い側にして、
/// ミュートの効きを速くする。48kHz で 5ms。
inline constexpr std::size_t kSendGainRampSamples = 240;

/// オーディオスレッドから送信スレッドへの、エンコード済みフレームの経路の数。
/// マイクとアプリ音声は別のデバイスの別のオーディオスレッドで動くので、SPSC キューを入力ごとに
/// 分ける（生産者を 1 つに保つ）。子機の画面で選べる入力はマイクとアプリ音声の 2 つ
/// （docs/adr/0035-client-input-expansion.md）。
inline constexpr std::size_t kCaptureLaneCount = 2;

/// マイクの経路。
inline constexpr std::size_t kMicrophoneLane = 0;

/// アプリ音声の経路。
inline constexpr std::size_t kAppAudioLane = 1;

/// session の tick() の間隔。session の推奨（100 ms〜1 s）の短い側にして、
/// 再接続の猶予の判定の遅れを小さくする。
inline constexpr std::chrono::milliseconds kSessionTickInterval{100};

/// 送信レベルのメーターを UI に送る間隔。仕様のメーターの頻度（10〜30 Hz）の中ほど。
inline constexpr std::chrono::milliseconds kMeterInterval{50};

/// ランデブーサーバーの UDP のポート（docs/adr/0012-rendezvous-protocol.md）。
inline constexpr std::uint16_t kRendezvousUdpPort = 3478;

/// コントロールスレッドの周期の処理（ClientRuntime::poll()）の間隔。親機からの制御メッセージと
/// ハンドシェイクの応答を処理するまでの遅れの上限になる。音声の経路（オーディオスレッドと
/// 送信スレッド）はこの周期を待たないので、制御の応答として気にならない 10ms にする。
inline constexpr std::chrono::milliseconds kPollInterval{10};

/// 子機の画面の大きさ。出演者が演奏しながら横に置ける小さな縦長の窓にし、親機の画面より
/// 小さくする（docs/adr/0005-ui-juce-webview.md）。最小はボタンとメーターが重ならない大きさ。
inline constexpr int kWindowWidth = 480;
inline constexpr int kWindowHeight = 720;
inline constexpr int kWindowMinWidth = 360;
inline constexpr int kWindowMinHeight = 560;

/// WebView2 のデータを置く、データフォルダの下のフォルダ名。同じデータフォルダで親機モードと
/// 同時に動かしても、WebView2 のブラウザープロセスを共有しないよう、モードごとに分ける。
inline constexpr const char* kWebViewDataFolderName = "webview2-client";

}  // namespace vocroz::client
