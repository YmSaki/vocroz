// SPDX-License-Identifier: AGPL-3.0-only
//
// 実ソケットの UDP 障害注入プロキシ（Linux、POSIX のソケット）。
// 子機（クライアント）からのデータグラムを上流（親機やランデブーサーバー）へ、上流からの応答をその子機へ
// 中継し、方向ごとの FaultProfile に従って遅らせ、捨て、複製し、壊す。障害の判定は結合テストと同じ
// FaultInjector を使うので、CI で再現した条件をそのまま実プロセスの組み合わせに持ち出せる。
// 子機の送信元ごとに上流側のソケットを分けるので、上流からは子機ごとに別の送信元ポートに見える（NAT
// と同じ）。
#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <queue>
#include <string>
#include <string_view>
#include <vector>

#include <netinet/in.h>

#include "vocroz/common/result.h"
#include "vocroz/netsim/fault_injector.h"
#include "vocroz/netsim/fault_profile.h"

namespace vocroz::tools {

/// "host:port" 形式の IPv4 アドレスをパースする。
/// @param text 例: "127.0.0.1:5000"
/// @return パースしたアドレス。書式の誤りなら nullopt
[[nodiscard]] std::optional<sockaddr_in> parse_ipv4_endpoint(std::string_view text);

/// プロキシの設定。
struct UdpFaultProxyConfig {
    /// 子機からの受信を待つアドレス。
    sockaddr_in listen{};
    /// 中継先（上流）のアドレス。
    sockaddr_in upstream{};
    /// 子機から上流への方向の障害。
    netsim::FaultProfile to_upstream;
    /// 上流から子機への方向の障害。
    netsim::FaultProfile to_client;
};

/// プロキシの統計。
struct UdpFaultProxyStats {
    std::uint64_t received = 0;    ///< 受信したデータグラム
    std::uint64_t forwarded = 0;   ///< 送り出したデータグラム（重複を含む）
    std::uint64_t lost = 0;        ///< 欠損させたデータグラム
    std::uint64_t duplicated = 0;  ///< 重複させたデータグラム
    std::uint64_t corrupted = 0;   ///< 改ざんしたデータグラム
    std::uint64_t clients = 0;     ///< 見つけた子機の送信元
};

/// UDP 障害注入プロキシ。
/// スレッド安全性: 1 つのスレッドから使う。
class UdpFaultProxy {
public:
    /// ソケットを開いて待ち受ける。
    /// @param config 設定
    /// @return 作ったプロキシ。ソケットを開けなければ原因のメッセージ
    [[nodiscard]] static common::Result<std::unique_ptr<UdpFaultProxy>, std::string> create(
        const UdpFaultProxyConfig& config);

    UdpFaultProxy(const UdpFaultProxy&) = delete;
    UdpFaultProxy& operator=(const UdpFaultProxy&) = delete;
    UdpFaultProxy(UdpFaultProxy&&) = delete;
    UdpFaultProxy& operator=(UdpFaultProxy&&) = delete;
    ~UdpFaultProxy();

    /// 受信と、期限の来た送信を 1 回分処理する。
    /// @param max_wait 受信を待つ上限。期限の来る送信があれば、それまでしか待たない
    void poll_once(std::chrono::milliseconds max_wait);

    /// 待ち受けているアドレスを返す（ポート 0 を指定したときに割り当てられたポートを知るため）。
    [[nodiscard]] sockaddr_in listen_address() const;

    /// 統計を返す。
    [[nodiscard]] const UdpFaultProxyStats& stats() const noexcept {
        return stats_;
    }

    /// create() からだけ呼ぶ。
    UdpFaultProxy(const UdpFaultProxyConfig& config, int listen_fd) noexcept;

private:
    using Clock = std::chrono::steady_clock;

    // 子機の送信元ごとの状態。
    struct Client {
        sockaddr_in address{};
        int upstream_fd = -1;
    };

    struct Pending {
        Clock::time_point due;
        std::uint64_t order;
        int fd;
        sockaddr_in to;
        std::vector<std::uint8_t> datagram;
    };

    struct LaterFirst {
        bool operator()(const Pending& a, const Pending& b) const noexcept {
            return a.due != b.due ? a.due > b.due : a.order > b.order;
        }
    };

    void receive_from_clients();
    void receive_from_upstream(Client& client);
    void schedule(netsim::FaultInjector& injector, int fd, const sockaddr_in& to,
                  const std::uint8_t* data, std::size_t size);
    void flush_due();
    Client* find_or_create_client(const sockaddr_in& from);

    UdpFaultProxyConfig config_;
    int listen_fd_;
    netsim::FaultInjector to_upstream_;
    netsim::FaultInjector to_client_;
    // キーは送信元の IPv4 アドレスとポートを 1 つの値にしたもの。
    std::map<std::uint64_t, Client> clients_;
    std::priority_queue<Pending, std::vector<Pending>, LaterFirst> pending_;
    std::uint64_t next_order_ = 0;
    UdpFaultProxyStats stats_;
};

}  // namespace vocroz::tools
