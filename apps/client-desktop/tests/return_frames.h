// SPDX-License-Identifier: AGPL-3.0-only
//
// テストで使う、返送のメディアの Opus のペイロードを作るヘルパー。親機の返送と同じ設定
// （低遅延プロファイルのモノラル）で、正弦波の 1 フレームをエンコードする。
#pragma once

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <vector>

#include "vocroz/transport/constants.h"
#include "vocroz/transport/opus_frame_encoder.h"

namespace vocroz::client::test {

/// 返送の 1 フレームの Opus のペイロードを作る。作れない環境ではテストを続けられないので止める。
inline std::vector<std::uint8_t> encode_return_frame() {
    auto encoder = transport::OpusFrameEncoder::create(transport::OpusEncoderConfig{});
    if (!encoder) {
        std::abort();
    }
    std::vector<float> pcm(encoder.value().frame_samples());
    for (std::size_t i = 0; i < pcm.size(); ++i) {
        // 1 kHz の正弦波。48 kHz で 48 サンプルが 1 周期。
        pcm[i] = 0.5F * static_cast<float>(
                            std::sin(2.0 * 3.141592653589793 * static_cast<double>(i) / 48.0));
    }
    std::vector<std::uint8_t> out(transport::kMaxOpusPayloadBytes);
    const auto written = encoder.value().encode(pcm, out);
    if (!written) {
        std::abort();
    }
    out.resize(written.value());
    return out;
}

/// 返送の 1 フレームのサンプル数（低遅延プロファイルの 5ms）。
inline constexpr std::uint64_t kReturnFrameSamples =
    transport::frame_samples(transport::FrameDuration::k5000Us);

}  // namespace vocroz::client::test
