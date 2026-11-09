// SPDX-License-Identifier: AGPL-3.0-only
//
// ルーティングの遅延（ms）とサンプル数の換算。
// UI は遅延を ms で扱い、ミキサー（audio-core）はサンプル数で扱う。session の検証と、
// コンポジションルートでミキサーのグラフへ変換するときで丸め方を食い違わせないよう、1
// か所で定める。
#pragma once

#include <cstddef>

namespace vocroz::session {

/// 遅延（ms）を、ミキサーのサンプルレートでのサンプル数に換算する。最も近い整数に丸める。
/// @param delay_ms 遅延（ms）。0 以上の有限の値（control-protocol のデコーダーが検証済み）
/// @return サンプル数
/// スレッド安全性: 純粋関数。どのスレッドからも呼び出し可能。
[[nodiscard]] std::size_t delay_samples(double delay_ms) noexcept;

}  // namespace vocroz::session
