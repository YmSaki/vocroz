// SPDX-License-Identifier: AGPL-3.0-only
//
// device のアダプター（WASAPI）のファクトリー関数。具象クラスは公開せず、
// コンポジションルートはここから audio-core のポートとして受け取る（AGENTS.md 5.1）。
// Windows でだけ定義する。
#pragma once

#include <memory>
#include <string_view>

#include "vocroz/audio_core/audio_device_error.h"
#include "vocroz/audio_core/ports/audio_device_catalog.h"
#include "vocroz/audio_core/ports/audio_input.h"
#include "vocroz/audio_core/ports/audio_output.h"
#include "vocroz/common/ports/logger.h"
#include "vocroz/common/result.h"

namespace vocroz::device {

/// WASAPI のデバイスの一覧を作る。
/// @param logger 一覧の取得に失敗したときの詳細（HRESULT）の出力先。戻り値より長く生存させる
/// @return デバイスの一覧
/// スレッド安全性: コントロールスレッドから呼ぶ。
[[nodiscard]] std::unique_ptr<audio_core::AudioDeviceCatalog> make_wasapi_audio_device_catalog(
    common::Logger& logger);

/// エンドポイント ID で指定した出力デバイスを、WASAPI の共有モードで開く出力を作る。
/// デバイスが 48kHz Float32 なら IAudioClient3 の低遅延共有モード（エンジンの最小周期）で、
/// そうでなければ通常の共有モードで開き、48kHz からの変換を OS に任せる。
/// 指定したデバイスだけを開き、OS の既定のデバイスや別のデバイスへは切り替えない。
/// 抜去と無効化は AudioDeviceListener::on_device_lost() で通知するだけで、ミュートの判断はしない。
/// オーディオスレッドは MMCSS の「Pro Audio」に登録する（docs/adr/0008-runtime-behavior.md）。
/// @param endpoint_id 出力デバイスのエンドポイント ID（AudioDeviceInfo::id）
/// @param logger      開始の失敗と抜去の詳細の出力先。戻り値より長く生存させる
/// @return 出力。ID のデバイスがない、無効、入力デバイスのときは kDeviceNotFound
/// スレッド安全性: コントロールスレッドから呼ぶ。
[[nodiscard]] common::Result<std::unique_ptr<audio_core::AudioOutput>, audio_core::AudioDeviceError>
make_wasapi_audio_output(std::string_view endpoint_id, common::Logger& logger);

/// エンドポイント ID で指定した入力デバイスを、WASAPI の共有モードで開く入力を作る。
/// 開き方、抜去の扱い、MMCSS への登録は make_wasapi_audio_output() と同じ。
/// 子機のマイク入力と、親機のローカル入力の両方に使う。
/// @param endpoint_id 入力デバイスのエンドポイント ID（AudioDeviceInfo::id）
/// @param logger      開始の失敗と抜去の詳細の出力先。戻り値より長く生存させる
/// @return 入力。ID のデバイスがない、無効、出力デバイスのときは kDeviceNotFound
/// スレッド安全性: コントロールスレッドから呼ぶ。
[[nodiscard]] common::Result<std::unique_ptr<audio_core::AudioInput>, audio_core::AudioDeviceError>
make_wasapi_audio_input(std::string_view endpoint_id, common::Logger& logger);

}  // namespace vocroz::device
