// SPDX-License-Identifier: AGPL-3.0-only
//
// device のアダプター（WASAPI、JUCE）のファクトリー関数。具象クラスは公開せず、
// コンポジションルートはここから audio-core のポートとして受け取る（AGENTS.md 5.1）。
// WASAPI のものは Windows でだけ、JUCE のもの（MIDI）は VOCROZ_WITH_JUCE が ON のときだけ定義する。
#pragma once

#include <memory>
#include <string_view>

#include "vocroz/audio_core/audio_device_error.h"
#include "vocroz/audio_core/ports/audio_device_catalog.h"
#include "vocroz/audio_core/ports/audio_input.h"
#include "vocroz/audio_core/ports/audio_output.h"
#include "vocroz/audio_core/ports/midi_device_catalog.h"
#include "vocroz/audio_core/ports/midi_input.h"
#include "vocroz/common/ports/clock.h"
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

/// JUCE の MidiInput による MIDI 入力ポートの一覧を作る（docs/adr/0029-midi-and-vsti.md）。
/// JUCE の MIDI はメッセージスレッドで動くので、ScopedJuceInitialiser_GUI を作り、
/// メッセージループを回すプロセスでだけ使う。
/// @return 一覧。list() は JUCE のメッセージスレッドから呼ぶ
/// スレッド安全性: JUCE のメッセージスレッドから呼ぶ。
[[nodiscard]] std::unique_ptr<audio_core::MidiDeviceCatalog> make_juce_midi_device_catalog();

/// ID で指定した MIDI 入力ポートを作る。ポートは start() で開き、ID のポートがなければ
/// start() が kDeviceNotFound を返す。別のポートへは切り替えない。
/// 受け取ったチャンネルメッセージは clock の時刻を付けてコールバックへ渡し、SysEx と
/// システムメッセージは捨てる。抜去は JUCE の切断の通知で検知し、
/// AudioDeviceListener::on_device_lost() で 1 回だけ（メッセージスレッドから）知らせる。
/// Panic の判断はしない。start() と stop() は JUCE のメッセージスレッドから呼ぶ。
/// @param identifier ポートの ID（MidiDeviceInfo::id）
/// @param clock      タイムスタンプの時計。Mixer
/// に注入するものと同じにする。戻り値より長く生存させる
/// @param logger     開始の失敗と抜去の出力先。戻り値より長く生存させる
/// @return MIDI 入力ポート
/// スレッド安全性: JUCE のメッセージスレッドから呼ぶ。
[[nodiscard]] std::unique_ptr<audio_core::MidiInput> make_juce_midi_input(
    std::string_view identifier, const common::Clock& clock, common::Logger& logger);

}  // namespace vocroz::device
