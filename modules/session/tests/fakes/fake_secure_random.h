// SPDX-License-Identifier: AGPL-3.0-only
//
// テスト用の SecureRandom。呼ぶたびに異なる、決まった並びのバイトを返す。
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "vocroz/session/ports/secure_random.h"

namespace vocroz::session {

/// 呼び出しの回数でバイトを埋める SecureRandom。テストを決定的にするために使う。
/// 先頭 8 バイトに呼び出しの通し番号を入れるので、呼ぶたびに必ず異なる値になる。
class FakeSecureRandom final : public SecureRandom {
public:
    void fill(std::span<std::uint8_t> out) noexcept override {
        ++calls_;
        for (std::size_t i = 0; i < out.size(); ++i) {
            out[i] =
                i < 8 ? static_cast<std::uint8_t>(calls_ >> (8 * i)) : static_cast<std::uint8_t>(i);
        }
    }

private:
    std::uint64_t calls_ = 0;
};

}  // namespace vocroz::session
