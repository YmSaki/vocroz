// SPDX-License-Identifier: AGPL-3.0-only
//
// ジッターバッファの深さを目標に保つよう、リサンプラーの変換比を決める PI 制御。
// 積分項が送信側と受信側のクロックのずれ（ドリフト）の推定値になる。
// ミュート解除後の新しい区間でも同じ送信側の時計なので、推定値は区間をまたいで保持する。
#pragma once

#include <cstddef>

namespace vocroz::transport {

/// 深さの誤差から変換比（出力 1 サンプルあたりに消費する入力のサンプル数）を決める。
/// スレッド安全性: 1 つのオブジェクトを同時に 1 スレッドからだけ使う（PlayoutStream
/// がオーディオスレッドで使う）。
class DriftController {
public:
    /// 深さを計測して変換比を更新する。
    /// @param depth_samples  現在の深さ（サンプル数）
    /// @param target_samples 目標の深さ（サンプル数）
    /// @param elapsed_samples 前回の更新から出力したサンプル数
    /// @return 次のブロックで使う変換比。1 ± kMaxRateDeviation の範囲
    /// スレッド安全性: オーディオスレッドから呼び出し可能。
    double update(double depth_samples, double target_samples,
                  std::size_t elapsed_samples) noexcept;

    /// 平滑化した深さを捨てる。ドリフトの推定値（積分項）は残す。
    /// 新しい区間の始まりで、前の区間の深さを引きずらないために呼ぶ。
    /// スレッド安全性: オーディオスレッドから呼び出し可能。
    void restart() noexcept;

    /// 現在の変換比を返す。
    [[nodiscard]] double ratio() const noexcept {
        return ratio_;
    }

    /// 推定したドリフト（変換比の 1 からのずれ）を返す。
    [[nodiscard]] double drift_estimate() const noexcept {
        return integral_;
    }

private:
    bool has_depth_ = false;
    double smoothed_depth_ = 0.0;
    double integral_ = 0.0;
    double ratio_ = 1.0;
};

}  // namespace vocroz::transport
