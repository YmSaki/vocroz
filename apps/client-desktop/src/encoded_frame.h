// SPDX-License-Identifier: AGPL-3.0-only
//
// オーディオスレッドでエンコードした 1 フレーム。SPSC キューで送信スレッドへ渡すので、
// トリビアルにコピーできる固定長の型にする（ヒープを確保しない）。
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "vocroz/transport/constants.h"

namespace vocroz::client {

/// エンコード済みのフレーム。
struct EncodedFrame {
    /// フレームの先頭のタイムスタンプ（48kHz のサンプル数）。
    std::uint64_t timestamp = 0;
    /// 送信ミュートの世代番号。
    std::uint32_t mute_epoch = 0;
    /// bytes のうち有効な長さ。
    std::size_t size = 0;
    /// Opus のペイロード。
    std::array<std::uint8_t, transport::kMaxOpusPayloadBytes> bytes{};

    /// 有効な範囲を返す。
    [[nodiscard]] std::span<const std::uint8_t> payload() const noexcept {
        return std::span<const std::uint8_t>(bytes).first(size);
    }
};

}  // namespace vocroz::client
