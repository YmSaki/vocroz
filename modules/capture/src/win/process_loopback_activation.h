// SPDX-License-Identifier: AGPL-3.0-only
//
// 自分のプロセスの音を除いたループバック（プロセスループバックの除外モード）を作る。
// 子機は返送モニターを同じプロセスで再生する。エンドポイントのループバックでは、既定の再生デバイスで
// 返送を聞くと返送音までアプリ音声として親機へ送り返し、相手に自分の声が戻る。自分のプロセスの音を
// OS の段階で除けば、返送の出力先と既定の再生デバイスが同じでも返送は収集に入らない
// （docs/adr/0035-client-input-expansion.md）。
#pragma once

#include <audioclient.h>
#include <windows.h>
#include <wrl/client.h>

namespace vocroz::capture::win {

/// このプロセスとその子プロセスが再生する音を除き、ほかのアプリが再生する音を集める
/// ループバックの IAudioClient を作る（ActivateAudioInterfaceAsync と
/// PROCESS_LOOPBACK_MODE_EXCLUDE_TARGET_PROCESS_TREE）。
/// 作った IAudioClient は、AUDCLNT_STREAMFLAGS_LOOPBACK を付けて Initialize() する。
/// @param client 作った IAudioClient を受け取る。失敗したときは空のまま
/// @return 結果。プロセスループバックのない Windows（Windows 10 2004 より前）では失敗を返す。
///         エンドポイントのループバックには切り替えない（返送音を拾うため。AGENTS.md 1.2）
/// スレッド安全性: COM を MTA で初期化したスレッドから呼ぶ。完了まで（上限
/// kActivationTimeoutMilliseconds）待つので、オーディオのループの外で呼ぶ。
[[nodiscard]] HRESULT activate_loopback_excluding_this_process(
    Microsoft::WRL::ComPtr<IAudioClient>& client) noexcept;

}  // namespace vocroz::capture::win
