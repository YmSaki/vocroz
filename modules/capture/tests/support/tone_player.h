// SPDX-License-Identifier: AGPL-3.0-only
//
// テストのプロセスから、既定の再生デバイスに正弦波を鳴らす。子機の返送モニターの代わりに使い、
// アプリ音声のループバックがこのプロセスの音を集めないことを確かめる。Windows でだけビルドする。
#pragma once

#include <atomic>
#include <thread>

namespace vocroz::capture::test_support {

/// 既定の再生デバイス（コンソールの役割）に、共有モードで正弦波を鳴らし続ける。
/// スレッド安全性: start() と stop() は同じスレッドから呼ぶ。COM のオブジェクトは専用のスレッドで
/// 作って使う。
class TonePlayer {
public:
    /// 鳴らす正弦波の振幅（フルスケールに対する比）。
    static constexpr float kAmplitude = 0.5F;

    TonePlayer() = default;
    ~TonePlayer();
    TonePlayer(const TonePlayer&) = delete;
    TonePlayer& operator=(const TonePlayer&) = delete;

    /// 鳴らし始める。
    /// @return 鳴らせたら true。再生デバイスがないか、ミックスフォーマットが 32 ビット浮動小数点で
    ///         なければ false（テストはスキップする）
    [[nodiscard]] bool start();

    /// 止めて、スレッドの終了を待つ。始めていなくてもよい。
    void stop() noexcept;

private:
    void run(std::atomic<int>& result) noexcept;

    std::thread thread_;
    std::atomic<bool> stopping_{false};
};

}  // namespace vocroz::capture::test_support
