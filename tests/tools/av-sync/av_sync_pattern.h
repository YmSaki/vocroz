// SPDX-License-Identifier: AGPL-3.0-only
//
// 試験パターンの設定。既定値のまま使えば、1 秒ごとに 100ms 光り、同時に 10ms のクリック音が鳴る。
#pragma once

#include <cstdint>
#include <vector>

#include "vocroz/common/result.h"

namespace vocroz::tools {
/// 試験パターンの設定。既定値のまま使えば、1 秒ごとに 100ms 光り、同時に 10ms のクリック音が鳴る。
struct AvSyncPattern {
    /// 音声のサンプリング周波数（Hz）
    int sample_rate = 48000;
    /// 映像のフレームレート（整数のみ）
    int fps = 60;
    /// 光る間隔
    int interval_ms = 1000;
    /// 光っている長さ
    int flash_ms = 100;
    /// クリック音の長さ
    int click_ms = 10;
    /// クリック音の周波数（正弦波）
    double click_hz = 1000.0;
    /// クリック音の振幅（0 より大きく 1 以下）
    double click_amplitude = 0.5;
    /// 全体の長さ
    int seconds = 10;
    /// 映像の幅（偶数）
    int width = 320;
    /// 映像の高さ（偶数）
    int height = 180;
    /// 音を映像よりこの時間だけずらす（正で遅らせる）。計測の試験用
    int audio_offset_ms = 0;
};

/// パターンの設定の誤り。
enum class AvSyncPatternError {
    /// 範囲外の値（0 以下の長さ、奇数の幅など）
    kInvalidValue,
    /// interval_ms や flash_ms がフレームの境界に乗らない
    kNotFrameAligned,
    /// |audio_offset_ms| が interval_ms / 2 以上
    kOffsetTooLarge,
};

/// 時刻の範囲とフレーム境界を検査し、最初の設定エラーを返します。
[[nodiscard]] common::Result<void, AvSyncPatternError> validate(const AvSyncPattern& pattern);
/// 呼び出す前に validate で設定を検証してください。
/// 完全なイベントを記録できる発光時刻をミリ秒で返します。
[[nodiscard]] std::vector<std::int64_t> event_times_ms(const AvSyncPattern& pattern);
/// 呼び出す前に validate で設定を検証してください。
/// 各イベントに指定した遅延を加えたモノラルのクリック音を返します。
[[nodiscard]] std::vector<float> generate_audio(const AvSyncPattern& pattern);
/// 呼び出す前に validate で設定を検証してください。
/// 各フレームの輝度を返します。
[[nodiscard]] std::vector<std::uint8_t> generate_frame_lumas(const AvSyncPattern& pattern);
}  // namespace vocroz::tools
