// SPDX-License-Identifier: AGPL-3.0-only
//
// プロセス内で UDP の経路を模すネットワーク。
// 端点（アドレス）ごとに受信の関数を登録し、送られたデータグラムを方向ごとの FaultProfile に従って
// 遅らせ、捨て、複製し、壊してから届ける。時刻は Clock から読むので、FakeClock を進めれば
// 実時間を待たずに、何秒分の通信でも決定的に再現できる。
// 結合テストでエンジンと子機をつなぐ経路として使う（docs/adr/0025-integration-test-harness.md）。
#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <queue>
#include <span>
#include <utility>
#include <vector>

#include "vocroz/common/ports/clock.h"
#include "vocroz/netsim/fault_injector.h"
#include "vocroz/netsim/fault_profile.h"

namespace vocroz::netsim {

/// シミュレーションのネットワーク上の端点のアドレス（IPv4 とポートに相当）。
struct NodeAddress {
    /// ホスト。値の意味はテストが決める（例: 1 が親機、2〜4 が子機）。
    std::uint32_t host = 0;
    /// ポート。
    std::uint16_t port = 0;

    friend constexpr auto operator<=>(const NodeAddress&, const NodeAddress&) = default;
};

/// 経路の途絶の扱い。
enum class OutageMode : std::uint8_t {
    kDrop,  ///< 途絶中のパケットを捨てる（回線が切れた）
    kHold,  ///< 途絶中のパケットを溜め、途絶が終わったらまとめて届ける（経路上のバッファが詰まった）
};

/// 1 方向の経路の統計。
struct LinkStats {
    std::uint64_t sent = 0;        ///< 送られたパケット
    std::uint64_t lost = 0;        ///< 欠損させたパケット
    std::uint64_t duplicated = 0;  ///< 重複させたパケット
    std::uint64_t reordered = 0;   ///< 順序を入れ替えるために遅らせたパケット
    std::uint64_t corrupted = 0;   ///< 1 ビットを反転させたパケット
    std::uint64_t dropped_in_outage = 0;  ///< 途絶（kDrop）で捨てたパケット
    std::uint64_t held_in_outage = 0;     ///< 途絶（kHold）で溜めたパケット
    std::uint64_t delivered = 0;  ///< 受信の関数に渡したパケット（重複を含む）
    std::uint64_t undeliverable = 0;  ///< 宛先が登録されていなくて捨てたパケット
};

/// プロセス内のネットワーク。
/// 受信の関数は deliver_due() の中から呼ぶ。受信の関数の中から send() を呼んでよい。
/// スレッド安全性: すべてのメンバー関数を 1 つのスレッドから呼ぶ。
class SimulatedNetwork {
public:
    /// 受信の関数。from は送信元、datagram は受信したデータグラム（呼び出しの間だけ有効）。
    using Receiver =
        std::function<void(const NodeAddress& from, std::span<const std::uint8_t> datagram)>;

    /// @param clock 時刻の取得元。このオブジェクトより長く生存させる
    explicit SimulatedNetwork(const common::Clock& clock) noexcept;

    /// 端点を登録する。登録済みなら受信の関数を置き換える。
    /// @param address  端点のアドレス
    /// @param receiver 受信の関数
    void attach(const NodeAddress& address, Receiver receiver);

    /// 端点の登録を外す。以降、その宛先のパケットは undeliverable
    /// として捨てる（プロセスの終了を模す）。
    /// @param address 端点のアドレス
    void detach(const NodeAddress& address);

    /// from から to
    /// への方向の障害を設定する。以降に送るパケットから適用し、乱数の列は設定し直すたびに
    /// 新しいシードから始める。
    /// @param from    送信元
    /// @param to      宛先
    /// @param profile 障害の設定
    void set_fault_profile(const NodeAddress& from, const NodeAddress& to,
                           const FaultProfile& profile);

    /// from から to への方向を途絶させる。
    /// @param from 送信元
    /// @param to   宛先
    /// @param mode 途絶中のパケットの扱い
    void begin_outage(const NodeAddress& from, const NodeAddress& to, OutageMode mode);

    /// from から to への方向の途絶を終える。kHold
    /// で溜めたパケットは、今の時刻から経路の遅延の後に届ける。
    /// @param from 送信元
    /// @param to   宛先
    void end_outage(const NodeAddress& from, const NodeAddress& to);

    /// データグラムを送る。到着時刻は今の時刻と経路の障害の設定で決まる。
    /// @param from     送信元
    /// @param to       宛先
    /// @param datagram 送るデータグラム。内容はコピーする
    void send(const NodeAddress& from, const NodeAddress& to,
              std::span<const std::uint8_t> datagram);

    /// 到着時刻が今の時刻以前のパケットを、到着時刻の順に受信の関数へ渡す。
    /// 同じ時刻のパケットは送った順に渡す。
    /// @return 渡したパケットの数
    std::size_t deliver_due();

    /// 次に届くパケットの到着時刻を返す。届くものがなければ nullopt。
    [[nodiscard]] std::optional<common::Clock::TimePoint> next_delivery_time() const;

    /// from から to への方向の統計を返す。
    [[nodiscard]] LinkStats stats(const NodeAddress& from, const NodeAddress& to) const;

private:
    struct Link {
        explicit Link(const FaultProfile& profile) : injector(profile) {}
        FaultInjector injector;
        LinkStats stats;
        std::optional<OutageMode> outage;
        // kHold の途絶中に溜めたパケット。
        std::vector<std::vector<std::uint8_t>> held;
    };

    struct InFlight {
        common::Clock::TimePoint arrival;
        // 同じ到着時刻のパケットを送った順に渡すための通し番号。
        std::uint64_t order;
        NodeAddress from;
        NodeAddress to;
        std::vector<std::uint8_t> datagram;
    };

    // priority_queue は最大を先頭にするので、到着の早いものが先頭に来るよう逆に比べる。
    struct LaterFirst {
        bool operator()(const InFlight& a, const InFlight& b) const noexcept {
            return a.arrival != b.arrival ? a.arrival > b.arrival : a.order > b.order;
        }
    };

    Link& link(const NodeAddress& from, const NodeAddress& to);
    void schedule(const NodeAddress& from, const NodeAddress& to, Link& link,
                  std::span<const std::uint8_t> datagram);

    const common::Clock& clock_;
    std::map<NodeAddress, Receiver> receivers_;
    std::map<std::pair<NodeAddress, NodeAddress>, Link> links_;
    std::priority_queue<InFlight, std::vector<InFlight>, LaterFirst> in_flight_;
    std::uint64_t next_order_ = 0;
};

}  // namespace vocroz::netsim
