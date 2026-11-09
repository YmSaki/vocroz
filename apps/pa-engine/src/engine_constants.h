// SPDX-License-Identifier: AGPL-3.0-only
//
// pa-engine の組み立てで使う定数。値の根拠は docs/adr/0028-engine-assembly.md。
#pragma once

#include <chrono>
#include <cstddef>

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

}  // namespace vocroz::engine
