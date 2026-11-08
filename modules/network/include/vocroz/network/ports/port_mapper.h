// SPDX-License-Identifier: AGPL-3.0-only
//
// ルーターに UDP のポート転送を自動で設定するポート（PCP、NAT-PMP、UPnP-IGD）。
// 親機が外から直接届くようにし、ホールパンチが成立しない回線でも直結できるようにする
// （docs/adr/0021-udp-socket-and-nat-traversal.md）。
// ルーターとの往復に数秒かかることがあるので、コントロールスレッドではなく作業用のスレッドから呼ぶ。
#pragma once

#include <chrono>
#include <cstdint>

#include "vocroz/common/result.h"
#include "vocroz/network/network_error.h"
#include "vocroz/network/udp_address.h"

namespace vocroz::network {

/// ポートの割り当てに使った方法。
enum class PortMappingMethod {
    kPcp,
    kNatPmp,
    kUpnpIgd,
};

/// ルーターが割り当てたポート転送。
struct PortMapping {
    /// 外から届く外部アドレス。ランデブーサーバーへの候補の先頭に入れる。
    UdpAddress external;
    /// 割り当ての寿命。0 なら無期限。使う側は寿命の半分で map() を呼び直して更新する。
    std::chrono::seconds lifetime{0};
    /// 割り当てに使った方法。
    PortMappingMethod method = PortMappingMethod::kPcp;
};

/// ポート転送の設定と解除。
/// スレッド安全性: 1 つのオブジェクトは 1 つのスレッドから使う。呼び出しはブロックする。
class PortMapper {
public:
    virtual ~PortMapper() = default;

    /// ローカルの UDP ポートへの転送を割り当てる。割り当て済みなら更新する。
    /// @param internal_port 転送先のローカルのポート（UdpEndpoint::local_port()）
    /// @param lifetime      希望する寿命。ルーターが短くすることがある
    /// @return 割り当てた転送。ゲートウェイがなければ kNoGateway、
    ///         対応していなければ kPortMappingUnsupported、断られたら kPortMappingRefused、
    ///         応答がなければ kTimedOut
    [[nodiscard]] virtual common::Result<PortMapping, NetworkError> map(
        std::uint16_t internal_port, std::chrono::seconds lifetime) = 0;

    /// 割り当てた転送を解除する。割り当てていなければ何もしない。失敗しても転送は寿命で消える。
    virtual void unmap() = 0;
};

}  // namespace vocroz::network
