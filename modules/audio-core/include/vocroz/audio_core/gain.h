// SPDX-License-Identifier: AGPL-3.0-only
//
// dB とリニアゲインの変換。
// フェーダーの値は dB で受け取り、オーディオスレッドではリニアゲインを掛けるので、
// 変換の規則（-∞ dB の扱い、上限）をこの 1 か所に集約する。
#pragma once

namespace vocroz::audio_core {

/// フェーダーの dB 値をリニアゲインに変換する。
/// @param db dB 値。-∞ と NaN は 0（無音）、kMaxFaderGainDb を超える値は上限に丸める
/// @return 0 以上 db_to_linear(kMaxFaderGainDb) 以下のリニアゲイン
/// エラー時の振る舞い: 不正な値（NaN）は無音側に倒す（フェイルセーフ）。
/// スレッド安全性: 純粋関数。どのスレッドからも呼び出し可能。オーディオスレッドから呼び出し可能。
[[nodiscard]] float fader_db_to_linear(float db) noexcept;

/// リニア値（ゲインやレベル）を dB に変換する。
/// @param linear リニア値。0 以下と NaN は -∞ dB
/// @return dB 値。メーター表示とテストに使う
/// スレッド安全性: 純粋関数。どのスレッドからも呼び出し可能。
[[nodiscard]] float linear_to_db(float linear) noexcept;

}  // namespace vocroz::audio_core
