// SPDX-License-Identifier: AGPL-3.0-only
//
// Winsock による UdpEndpoint。デュアルスタックの UDP ソケット 1 本と、受信を待つスレッドを持つ
// （docs/adr/0021-udp-socket-and-nat-traversal.md）。
// 受信スレッドは WSAEventSelect
// のイベントと停止用のイベントを同時に待つ。停止用のイベントで待ちを解くので、
// ソケットを別スレッドから閉じて受信を中断させる（閉じたハンドルの再利用と競合しうる）必要がない。
#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <span>
#include <thread>

#include <winsock2.h>

#include "vocroz/common/result.h"
#include "vocroz/network/network_error.h"
#include "vocroz/network/ports/datagram_listener.h"
#include "vocroz/network/ports/udp_endpoint.h"

#include "win/socket_handle.h"
#include "win/winsock_runtime.h"

namespace vocroz::network::win {

/// Winsock の UDP ソケット。
/// スレッド安全性: send_to() と local_port() はどのスレッドからも同時に呼び出し可能
/// （UDP の sendto() は Winsock がスレッド安全に扱う）。生成と破棄はコントロールスレッドから。
class WinsockUdpEndpoint final : public UdpEndpoint {
public:
    /// ソケットを開いてバインドし、受信スレッドを起動する。
    /// @param port     バインドするポート。0 なら OS が選ぶ
    /// @param listener 受信したデータグラムの渡し先
    /// @return ソケット。作れなければ kSocketUnavailable、バインドできなければ kBindFailed
    [[nodiscard]] static common::Result<std::unique_ptr<WinsockUdpEndpoint>, NetworkError> open(
        std::uint16_t port, DatagramListener& listener);

    /// 受信スレッドを止めて待ち、ソケットを閉じる。
    ~WinsockUdpEndpoint() override;
    WinsockUdpEndpoint(const WinsockUdpEndpoint&) = delete;
    WinsockUdpEndpoint& operator=(const WinsockUdpEndpoint&) = delete;

    [[nodiscard]] common::Result<void, NetworkError> send_to(
        const UdpAddress& to, std::span<const std::uint8_t> datagram) noexcept override;

    [[nodiscard]] std::uint16_t local_port() const noexcept override {
        return local_port_;
    }

private:
    explicit WinsockUdpEndpoint(DatagramListener& listener) noexcept;

    // ソケットを作って設定し、バインドする。
    [[nodiscard]] common::Result<void, NetworkError> bind_socket(std::uint16_t port);
    // 受信スレッドの本体。
    void receive_loop() noexcept;
    // 届いているデータグラムをすべて読み、listener に渡す。
    void drain() noexcept;

    WinsockRuntime runtime_;
    DatagramListener& listener_;
    SocketHandle socket_;
    // IPv6 のソケットを作れず、IPv4 だけで開いたら false。
    bool dual_stack_ = true;
    std::uint16_t local_port_ = 0;
    EventHandle readable_;
    EventHandle stop_;
    std::thread receiver_;
};

}  // namespace vocroz::network::win
