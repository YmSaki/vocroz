// SPDX-License-Identifier: AGPL-3.0-only
//
// capture のアダプター（WASAPI のループバック）のファクトリー関数。具象クラスは公開せず、
// コンポジションルートはここから audio-core のポートとして受け取る（AGENTS.md 5.1）。
// Windows でだけ定義する。
#pragma once

#include <memory>

#include "vocroz/audio_core/audio_device_error.h"
#include "vocroz/audio_core/ports/audio_input.h"
#include "vocroz/common/ports/logger.h"
#include "vocroz/common/result.h"

namespace vocroz::capture {

/// 既定の再生デバイス（コンソールの役割）の音を、WASAPI のループバックで集める入力を作る。
/// コールバックには、常に 48kHz Float32 のステレオで渡す。
/// デバイスのミックスフォーマットとの違い（レートやチャンネル数）は、OS に変換させる。
/// 何も再生していない間も無音を渡し続け、送り側の時間を止めない（ADR-0035）。
/// 既定の再生デバイスが切り替わったか、再生デバイスが抜けたら、ストリームを止めて
/// AudioDeviceListener::on_device_lost() を 1 回呼ぶ。開き直しは呼び出し側が決める。
/// オーディオスレッドは MMCSS の「Pro Audio」に登録する（docs/adr/0008-runtime-behavior.md）。
/// @param logger 開始の失敗と抜去の詳細の出力先。戻り値より長く生存させる
/// @return 入力。生成そのものは失敗しない（ポートの形をそろえるため Result で返す）。
///         既定の再生デバイスは start() で開くので、ないときは start() が kDeviceNotFound を返す
/// スレッド安全性: コントロールスレッドから呼ぶ。
[[nodiscard]] common::Result<std::unique_ptr<audio_core::AudioInput>, audio_core::AudioDeviceError>
make_wasapi_loopback_input(common::Logger& logger);

}  // namespace vocroz::capture
