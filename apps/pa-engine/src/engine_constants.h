// SPDX-License-Identifier: AGPL-3.0-only
//
// pa-engine の組み立てで使う定数。値の根拠は docs/adr/0028-engine-assembly.md。
#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>

namespace vocroz::engine {

/// 終了コード（docs/adr/0007-launch-interface.md）。
inline constexpr int kExitOk = 0;
/// 起動後の致命的なエラー。原因はログに書く。
inline constexpr int kExitFatal = 1;
/// 引数の誤り。
inline constexpr int kExitUsage = 2;
/// 同じデータフォルダのエンジンがすでに動いている。remote-pa はそのエンジンにつなげばよいので、
/// 「既存のインスタンスに任せて終了した」の 3 を使う。
inline constexpr int kExitAlreadyRunning = 3;

/// コントロールスレッドの周期処理の間隔。session の tick() は 100 ms〜1 s で呼ぶ約束で、
/// メーターは 10〜30 Hz で送る。メーターに合わせた 50 ms（20 Hz）で回し、tick() も同じ周期で呼ぶ。
inline constexpr std::chrono::milliseconds kControlTickInterval{50};

/// session の tick() を呼ぶ間隔。招待の期限と再接続の猶予は秒単位なので、100 ms で足りる。
inline constexpr std::chrono::milliseconds kSessionTickInterval{100};

/// セッションも UI もない状態がこの時間続いたら、エンジンを終了する。
/// remote-pa を閉じたあとにエンジンだけが残り続けないようにする。UI が落ちて利用者が
/// remote-pa を起動し直すまでの時間より十分に長くし、起動し直した UI がつながる前に
/// 終わってしまわないようにする。
inline constexpr std::chrono::seconds kIdleShutdownTimeout{30};

/// ミキサーのフェーダーとメーターの数。ストリップ 64 本、Master 1、ルーティングのバス 32 本、
/// センド 128 本の合計（225）に、出力のメーター 32 個を足しても足りる 2 のべき乗。
inline constexpr std::size_t kMixerFaderCapacity = 256;
inline constexpr std::size_t kMixerMeterCapacity = 256;

/// 古いクラッシュダンプを消して残す数。起動時に 1 つ空けておき、次のクラッシュで 5 件になる
/// （docs/adr/0008-runtime-behavior.md の「最新の 5 件だけを残す」）。
inline constexpr std::size_t kCrashDumpsKeptAtStartup = 4;

/// 同時に持つ子機との接続の上限。ストリップの上限（64 本）と同じにする。張り直しの途中の古い接続も
/// 数えるので、上限に達したら新しい鍵交換を受け付けない（古い接続が閉じれば空く）。
inline constexpr std::size_t kMaxPeerConnections = 64;

/// コントロールスレッドからネットワーク受信スレッドへの指示（接続の追加、承認、送信ミュート、
/// 取り外し）のキューの長さ。接続ごとに数個しか積まないので、上限の接続数の 4 倍で足りる。
/// 満杯なら次の周期で積み直す。
inline constexpr std::size_t kReceiveCommandCapacity = 256;

/// ネットワーク受信スレッドを起こすデータグラムを自分のポートへ送る間隔。受信スレッドは
/// データグラムが届くまで待つので、子機の音が途絶えたときの信号状態の判定（500 ms で NO_SIGNAL）
/// と指示の反映が、データグラムの来ない間も遅れないようにする。
/// 間隔は判定の粒度（100 ms 程度）に合わせる。
inline constexpr std::chrono::milliseconds kReceiveWakeInterval{100};

/// MIDI 入力ポートの一覧を読み直す間隔。抜き差しした MIDI デバイスを、この間隔のうちに開く。
/// 一覧は JUCE のメッセージループが更新するキャッシュから返るので、読み直しは軽い。
/// 演奏の途中で挿し直したときに、待たされたと感じない程度にする。
inline constexpr std::chrono::milliseconds kMidiDeviceScanInterval{1000};

/// ランデブーサーバーの UDP のポート。infra/rendezvous の既定値で、
/// 子機の client_constants.h と同じ値。
inline constexpr std::uint16_t kRendezvousUdpPort = 3478;

}  // namespace vocroz::engine
