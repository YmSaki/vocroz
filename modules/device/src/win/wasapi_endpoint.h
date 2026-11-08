// SPDX-License-Identifier: AGPL-3.0-only
//
// WASAPI のエンドポイントを ID で開く処理と、HRESULT の扱い。
// 出力、入力、一覧の 3 つのアダプターが同じ規則（ID の解決、エラーの分類）で使う。
#pragma once

#include <cstddef>
#include <string_view>

#include <audioclient.h>
#include <mmdeviceapi.h>
#include <mmreg.h>
#include <windows.h>
#include <wrl/client.h>

#include "vocroz/audio_core/audio_device_error.h"
#include "vocroz/common/ports/logger.h"
#include "vocroz/common/result.h"

#include "detail/stream_plan.h"

namespace vocroz::device::win {

/// KSDATAFORMAT_SUBTYPE_IEEE_FLOAT。ksmedia.h の定義は GUID の実体を別にリンクする必要があるため、
/// 値を直接書く（WAVE_FORMAT_IEEE_FLOAT を埋め込んだ標準の GUID）。
inline constexpr GUID kSubtypeIeeeFloat = {
    0x00000003, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}};

/// WASAPI の HRESULT を、ポートが返すエラーの原因に分類する。
/// @param hr 失敗した HRESULT
/// @return 原因。分類できなければ kInternal
[[nodiscard]] audio_core::AudioDeviceError to_device_error(HRESULT hr) noexcept;

/// 失敗した HRESULT をログに出す。
/// @param logger  出力先
/// @param level   重要度
/// @param context 何をしていて失敗したか（英語の短い句）
/// @param hr      失敗した HRESULT
/// スレッド安全性: オーディオスレッドのループの外から呼ぶ。
void log_hresult(common::Logger& logger, common::LogLevel level, std::string_view context,
                 HRESULT hr) noexcept;

/// エンドポイント ID でデバイスを開く。
/// 既定のデバイスへは決して置き換えない。ID が解決できなければ失敗にする。
/// @param endpoint_id エンドポイント ID（UTF-8）
/// @param flow        期待する向き（eRender か eCapture）
/// @param logger      失敗の詳細の出力先
/// @return デバイス。ID がない、有効でない、向きが違うときは kDeviceNotFound
/// 前提条件: 呼び出したスレッドで COM が初期化されている。
[[nodiscard]] common::Result<Microsoft::WRL::ComPtr<IMMDevice>, audio_core::AudioDeviceError>
open_endpoint(std::string_view endpoint_id, EDataFlow flow, common::Logger& logger);

/// WAVEFORMATEX をストリームの判断に使う要約に変換する。
[[nodiscard]] detail::MixFormat summarize_format(const WAVEFORMATEX& format) noexcept;

/// 内部フォーマット（48kHz、Float32、インターリーブ）の WAVEFORMATEXTENSIBLE を作る。
/// 通常の共有モードで、OS にこのフォーマットへの変換を任せるときに使う。
/// @param channels     チャンネル数
/// @param channel_mask スピーカー配置。デバイスのミックスフォーマットの値をそのまま渡す（不明なら
/// 0）
[[nodiscard]] WAVEFORMATEXTENSIBLE make_internal_format(std::size_t channels,
                                                        DWORD channel_mask) noexcept;

/// WAVEFORMATEX のスピーカー配置を返す。WAVEFORMATEXTENSIBLE でなければ 0（不明）。
[[nodiscard]] DWORD channel_mask_of(const WAVEFORMATEX& format) noexcept;

/// デバイスの共有モードのフォーマット（ミックスフォーマット）の要約を読む。
/// @param device 対象のデバイス
/// @param logger 失敗の詳細の出力先
/// @return ミックスフォーマットの要約
/// 前提条件: 呼び出したスレッドで COM が初期化されている。
[[nodiscard]] common::Result<detail::MixFormat, audio_core::AudioDeviceError> read_mix_format(
    IMMDevice& device, common::Logger& logger);

}  // namespace vocroz::device::win
