// SPDX-License-Identifier: AGPL-3.0-only
//
// SRTP 外部出力の宛先ごとの UDP ソケットを開くポート（docs/adr/0040-srtp-external-output.md）。
// 子機との接続のソケット（ランデブーとホールパンチに使う 1 本）とは分け、宛先ごとに 1 本開く。
// 外部出力の送信は独自トランスポートの経路と独立させるため。
// 本番は network の名前解決と Winsock のソケットで実装し（NetworkExternalSocketOpener）、
// テストはフェイクに差し替える。
#pragma once

#include <cstdint>
#include <memory>
#include <string_view>

#include "vocroz/common/result.h"
#include "vocroz/control_protocol/states.h"
#include "vocroz/transport/ports/datagram_socket.h"

namespace vocroz::engine {

/// 外部出力の宛先へ送るソケットを開く。
/// スレッド安全性: コントロールスレッドから呼ぶ。名前解決でブロックしうる。
class ExternalSocketOpener {
public:
    virtual ~ExternalSocketOpener() = default;

    /// 宛先へ送るソケットを開く。
    /// @param host 宛先のホスト名か IP アドレス
    /// @param port 宛先の UDP ポート
    /// @return ソケット（送信は 1 つの送信スレッドから行う）。名前解決できなければ kResolveFailed、
    ///         ソケットを開けなければ kSendFailed
    [[nodiscard]] virtual common::Result<std::unique_ptr<transport::DatagramSocket>,
                                         control_protocol::ExternalOutputResult>
    open(std::string_view host, std::uint16_t port) = 0;
};

}  // namespace vocroz::engine
