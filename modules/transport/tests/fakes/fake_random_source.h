// SPDX-License-Identifier: AGPL-3.0-only
//
// テスト用の RandomSource。テストが決めた値を返すので、ジッターを決定的に検証できる。
#pragma once

#include <cstdint>

#include "vocroz/transport/ports/random_source.h"

namespace vocroz::transport {

/// value を返し続ける RandomSource。
class FakeRandomSource final : public RandomSource {
public:
    /// value を返す。
    [[nodiscard]] std::uint32_t next_uint32() noexcept override {
        ++calls;
        return value;
    }

    /// 返す値。
    std::uint32_t value = 0;
    /// next_uint32() が呼ばれた回数。
    int calls = 0;
};

}  // namespace vocroz::transport
