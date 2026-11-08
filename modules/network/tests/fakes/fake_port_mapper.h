// SPDX-License-Identifier: AGPL-3.0-only
//
// テスト用の PortMapper。テストが決めた結果を返し、呼ばれた回数を記録する。
#pragma once

#include <chrono>
#include <cstdint>

#include "vocroz/network/ports/port_mapper.h"

namespace vocroz::network {

/// 決めた結果を返す PortMapper。
class FakePortMapper final : public PortMapper {
public:
    [[nodiscard]] common::Result<PortMapping, NetworkError> map(
        std::uint16_t internal_port, std::chrono::seconds lifetime) override {
        ++map_calls;
        last_internal_port = internal_port;
        last_lifetime = lifetime;
        if (!succeed) {
            return common::make_unexpected(error);
        }
        return mapping;
    }

    void unmap() override {
        ++unmap_calls;
    }

    /// true なら mapping を、false なら error を返す。
    bool succeed = false;
    PortMapping mapping;
    NetworkError error = NetworkError::kPortMappingUnsupported;
    int map_calls = 0;
    int unmap_calls = 0;
    std::uint16_t last_internal_port = 0;
    std::chrono::seconds last_lifetime{0};
};

}  // namespace vocroz::network
