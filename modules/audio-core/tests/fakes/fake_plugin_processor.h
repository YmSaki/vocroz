// SPDX-License-Identifier: AGPL-3.0-only
//
// テスト用の PluginProcessor。報告した遅延のぶんだけ入力を実際に遅らせ、ゲインを掛けて返す。
// 遅延補正（PDC）のテストで、補正の有無が出力のサンプルの並びとして確かめられるようにする。
// バッファは構築時に確保するので、RealtimeSanitizer を有効にしたテストでもそのまま使える。
#pragma once

#include <array>
#include <cstddef>
#include <limits>
#include <utility>
#include <vector>

#include "vocroz/audio_core/ports/plugin_processor.h"

namespace vocroz::audio_core {

/// 決定的に動作するプラグイン。
/// スレッド安全性: 設定の変更はグラフに渡す前に行う。process() はオーディオスレッド（テストの
/// スレッド）だけが呼ぶ。
class FakePluginProcessor final : public PluginProcessor {
public:
    /// @param layout 入出力のチャンネル数
    /// @param latency_samples 報告し、実際に遅らせる遅延
    /// @param gain 出力に掛けるゲイン（リニア値）
    explicit FakePluginProcessor(PluginBusLayout layout = {}, std::size_t latency_samples = 0,
                                 float gain = 1.0F)
        : layout_(layout), latency_(latency_samples), gain_(gain) {
        for (auto& delay : delays_) {
            delay.assign(latency_samples, 0.0F);
        }
    }

    /// 処理を n 回成功させた後、kFailed を返すようにする。
    void fail_after(std::size_t n) noexcept {
        fail_after_ = n;
    }

    /// 処理を n 回成功させた後、出力を NaN にするようにする。
    void produce_nan_after(std::size_t n) noexcept {
        nan_after_ = n;
    }

    /// process() が呼ばれた回数を返す。
    [[nodiscard]] std::size_t process_count() const noexcept {
        return process_count_;
    }

    /// 最後の process() に渡されたチャンネル数を返す。
    [[nodiscard]] std::size_t last_channel_count() const noexcept {
        return last_channel_count_;
    }

    [[nodiscard]] PluginBusLayout layout() const noexcept override {
        return layout_;
    }

    [[nodiscard]] std::size_t latency_samples() const noexcept override {
        return latency_;
    }

    [[nodiscard]] PluginProcessStatus process(const PluginProcessContext& context) noexcept
        VOCROZ_NONBLOCKING override {
        const std::size_t call = process_count_++;
        last_channel_count_ = context.channels.size();
        if (call >= fail_after_) {
            return PluginProcessStatus::kFailed;
        }
        const bool nan = call >= nan_after_;
        // 入力を出力へ写す。入力がモノラルで出力がステレオなら、両チャンネルに同じ信号を出す。
        for (std::size_t ch = 0; ch < layout_.output_channels; ++ch) {
            const std::size_t from = layout_.input_channels == 0   ? ch
                                     : ch < layout_.input_channels ? ch
                                                                   : 0;
            float* samples = context.channels[ch];
            const float* input = context.channels[from];
            for (std::size_t i = 0; i < context.frames; ++i) {
                float sample = layout_.input_channels == 0 ? 0.0F : input[i];
                if (!delays_[ch].empty()) {
                    std::swap(sample, delays_[ch][positions_[ch]]);
                    positions_[ch] = (positions_[ch] + 1) % delays_[ch].size();
                }
                samples[i] = nan ? std::numeric_limits<float>::quiet_NaN() : sample * gain_;
            }
        }
        return PluginProcessStatus::kOk;
    }

private:
    PluginBusLayout layout_;
    std::size_t latency_;
    float gain_;
    std::array<std::vector<float>, 2> delays_;
    std::array<std::size_t, 2> positions_{};
    std::size_t fail_after_ = std::numeric_limits<std::size_t>::max();
    std::size_t nan_after_ = std::numeric_limits<std::size_t>::max();
    std::size_t process_count_ = 0;
    std::size_t last_channel_count_ = 0;
};

}  // namespace vocroz::audio_core
