// SPDX-License-Identifier: AGPL-3.0-only
//
// 複数のポート開放の方法を順に試す PortMapper。

#include "chained_port_mapper.h"

#include <utility>

namespace vocroz::network {

ChainedPortMapper::ChainedPortMapper(std::vector<std::unique_ptr<PortMapper>> mappers)
    : mappers_(std::move(mappers)) {}

common::Result<PortMapping, NetworkError> ChainedPortMapper::map(std::uint16_t internal_port,
                                                                 std::chrono::seconds lifetime) {
    if (active_) {
        auto renewed = mappers_[*active_]->map(internal_port, lifetime);
        if (renewed) {
            return renewed;
        }
        // 更新に失敗したら（ルーターの交換や設定の変更）、ほかの方法も含めて最初から試す。
        active_.reset();
    }
    NetworkError last_error = NetworkError::kPortMappingUnsupported;
    for (std::size_t i = 0; i < mappers_.size(); ++i) {
        auto mapped = mappers_[i]->map(internal_port, lifetime);
        if (mapped) {
            active_ = i;
            return mapped;
        }
        last_error = mapped.error();
    }
    return common::make_unexpected(last_error);
}

void ChainedPortMapper::unmap() {
    if (active_) {
        mappers_[*active_]->unmap();
        active_.reset();
    }
}

}  // namespace vocroz::network
