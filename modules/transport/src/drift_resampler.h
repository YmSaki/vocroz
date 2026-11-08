// SPDX-License-Identifier: AGPL-3.0-only
//
// クロックドリフト補正のための、変換比が 1 にごく近い可変レートのリサンプラー。
// 子機と親機のオーディオクロックの ppm 単位のずれでジッターバッファが溢れたり枯渇したりしないよう、
// PlayoutStream が深さに応じて変換比を微小に変える。
// 補間はカイザー窓付き sinc の多相フィルターで行い、位相の間は線形補間する。
// 変換比が 1
// に近いので、折り返しよりも、小数位相が変わっても高域の特性が揺れないことを優先して設計している。
#pragma once

#include <array>
#include <cstddef>
#include <span>
#include <vector>

namespace vocroz::transport {

/// 入力を内部の FIFO に積み、変換比に従って出力を取り出す可変レートのリサンプラー。
/// 領域はコンストラクターで確保し、以降の操作はヒープを確保しない。
/// スレッド安全性: 1 つのオブジェクトを同時に 1 スレッドからだけ使う（PlayoutStream
/// がオーディオスレッドで使う）。
class DriftResampler {
public:
    /// フィルターの片側のタップ数。全体の遅延はこのサンプル数（48kHz で約 0.33ms）になる。
    static constexpr std::size_t kHalfTaps = 16;

    /// @param max_push_samples 1 回の push() で渡す最大のサンプル数
    explicit DriftResampler(std::size_t max_push_samples);

    /// 入力を FIFO の末尾に積む。
    /// @param input 積むサンプル
    /// @return 積めたら true。領域が足りなければ何もせず false
    /// スレッド安全性: オーディオスレッドから呼び出し可能。
    [[nodiscard]] bool push(std::span<const float> input) noexcept;

    /// 出力を取り出す。
    /// @param out   書き込み先。入力が足りる範囲で先頭から埋める
    /// @param ratio 出力 1 サンプルあたりに消費する入力のサンプル数。1.0
    /// より大きいと入力を速く消費する
    /// @return 書き込んだサンプル数。入力が足りなければ out.size() より少ない
    /// スレッド安全性: オーディオスレッドから呼び出し可能。
    [[nodiscard]] std::size_t pull(std::span<float> out, double ratio) noexcept;

    /// まだ出力に使い終えていない入力のサンプル数を返す。深さの計測に使う。
    [[nodiscard]] double buffered_samples() const noexcept;

    /// FIFO を空にし、過去の入力を無音として扱う初期状態に戻す。
    /// スレッド安全性: オーディオスレッドから呼び出し可能。
    void reset() noexcept;

private:
    // 小数位相の分割数。位相の間を線形補間するので、係数の誤差は -90dB 程度に収まる。
    static constexpr std::size_t kPhases = 256;
    static constexpr std::size_t kTaps = kHalfTaps * 2;
    using Kernel = std::array<float, kTaps>;

    // 小数位相 0〜kPhases（両端を含む）の係数表を作る。
    static std::array<Kernel, kPhases + 1> make_kernels();
    void compact() noexcept;

    std::array<Kernel, kPhases + 1> kernels_;
    std::vector<float> fifo_;
    // fifo_ の有効な末尾。
    std::size_t size_ = 0;
    // 次の出力の中心になる入力の位置（整数部と小数部）。
    std::size_t index_ = 0;
    double fraction_ = 0.0;
};

}  // namespace vocroz::transport
