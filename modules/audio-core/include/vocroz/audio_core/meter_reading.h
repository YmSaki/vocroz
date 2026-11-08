// SPDX-License-Identifier: AGPL-3.0-only
//
// レベルメーターの読み取り結果。
#pragma once

namespace vocroz::audio_core {

/// レベルメーターの値（リニア値。dB への変換は linear_to_db()）。
struct MeterReading {
    float peak = 0.0F;  ///< 前回の読み取りから今回までのサンプルの絶対値の最大値
    float rms = 0.0F;  ///< 時定数 300ms で平滑化した RMS
};

}  // namespace vocroz::audio_core
