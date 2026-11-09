// SPDX-License-Identifier: AGPL-3.0-only
//
// 結合テストの親機（pa-engine の代わり）。
// session の HostSession を、transport の部品（Noise_IK
// の応答側、暗号化、信頼制御層、死活監視、再生）と
// シミュレーションのネットワークにつなぐ。接続ごとに PeerHandle を振り、PeerLink ポートで
// 承認（admit）されるまで、その接続のメディアをミキサーへ流さない。承認後は、子機の入力の申告に
// 従ってストリーム ID ごとに再生側を作り、それぞれのストリップへ流す。
// UI、ランデブーサーバー、設定、乱数は session のフェイクを使い、テストから直接操作する。
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "vocroz/common/ports/clock.h"
#include "vocroz/netsim/simulated_network.h"
#include "vocroz/session/host_session.h"
#include "vocroz/session/ports/peer_link.h"
#include "vocroz/test_harness/harness_mixer.h"
#include "vocroz/transport/playout_stream.h"
#include "vocroz/transport/static_key_pair.h"

#include "fakes/fake_logger.h"
#include "fakes/fake_output_device_control.h"
#include "fakes/fake_playout_control.h"
#include "fakes/fake_rendezvous_client.h"
#include "fakes/fake_secure_random.h"
#include "fakes/fake_settings_store.h"
#include "fakes/fake_ui_event_sink.h"

namespace vocroz::test_harness {

/// 接続ごとの統計。
struct HostPeerStats {
    /// 承認前に届いて捨てたメディアフレーム。
    std::uint64_t media_before_admission = 0;
    /// 承認後に再生側へ渡したメディアフレーム。
    std::uint64_t media_admitted = 0;
    /// 承認後に届いたが、申告のないストリームなので捨てたメディアフレーム。
    std::uint64_t media_unrouted = 0;
};

/// 1 本のストリップに流し込んだ、直前の render() のブロック（フェーダーとミュートを掛ける前）。
/// モノラルのストリップは左右に同じ値を入れる。
struct StripOutput {
    std::vector<float> left;
    std::vector<float> right;
};

/// 結合テストの親機。
/// スレッド安全性: 1 つのスレッドから使う（テストでは仮想時刻を進めるスレッドが、
/// コントロールスレッド、ネットワーク受信スレッド、オーディオスレッドの役をすべてする）。
class SimulatedHost final : private session::PeerLink {
public:
    /// @param clock   時刻の取得元。このオブジェクトより長く生存させる
    /// @param network ネットワーク。このオブジェクトより長く生存させる
    /// @param address 親機のアドレス
    /// @param playout 接続ごとの再生の設定
    SimulatedHost(const common::Clock& clock, netsim::SimulatedNetwork& network,
                  const netsim::NodeAddress& address, const transport::PlayoutConfig& playout);
    SimulatedHost(const SimulatedHost&) = delete;
    SimulatedHost& operator=(const SimulatedHost&) = delete;
    SimulatedHost(SimulatedHost&&) = delete;
    SimulatedHost& operator=(SimulatedHost&&) = delete;
    ~SimulatedHost() override;

    /// 親機の静的公開鍵（招待で子機に渡る）。
    [[nodiscard]] const transport::PublicKey& public_key() const noexcept;

    /// セッション管理。UI のコマンドとランデブーサーバーの結果は、テストがここへ直接渡す。
    [[nodiscard]] session::HostSession& session() noexcept {
        return *session_;
    }

    /// UI へのメッセージの記録。
    [[nodiscard]] const session::FakeUiEventSink& ui() const noexcept {
        return ui_;
    }

    /// ランデブーサーバーへの要求の記録。
    [[nodiscard]] const session::FakeRendezvousClient& rendezvous() const noexcept {
        return rendezvous_;
    }

    /// ミキサー。
    [[nodiscard]] const HarnessMixer& mixer() const noexcept {
        return mixer_;
    }

    /// 時間を進めたあとの処理（信頼制御層の再送、死活監視、保留した通知の配送、session の tick）。
    /// ネットワークの配送のあとに呼ぶ。
    void step();

    /// 出力のコールバック（1 ブロック分の Master の PCM を書き込む）。
    /// @param out 書き込み先
    void render(std::span<float> out);

    /// ストリップに流し込んだ直前の render() のブロック。流し込んでいないストリップなら nullptr。
    /// @param strip_id ストリップの entity_id
    [[nodiscard]] const StripOutput* strip_output(const std::string& strip_id) const {
        const auto it = strip_outputs_.find(strip_id);
        return it == strip_outputs_.end() ? nullptr : &it->second;
    }

    /// 接続の数（閉じたものを除く）。
    [[nodiscard]] std::size_t peer_count() const noexcept;

    /// すべての接続の統計の合計。閉じた接続の分も含む。
    [[nodiscard]] HostPeerStats total_stats() const noexcept;

private:
    struct Route;
    struct Peer;

    // session::PeerLink
    void send(session::PeerHandle peer, std::span<const std::uint8_t> message) override;
    void admit(session::PeerHandle peer, const std::vector<session::StreamRoute>& routes) override;
    void apply_send_mute(session::PeerHandle peer, bool send_muted, std::uint32_t epoch) override;
    void set_return_mute(session::PeerHandle peer, bool muted, std::uint32_t epoch) override;
    void close(session::PeerHandle peer) override;

    void on_datagram(const netsim::NodeAddress& from, std::span<const std::uint8_t> datagram);
    void accept_handshake(const netsim::NodeAddress& from, std::span<const std::uint8_t> datagram);
    Peer* find(session::PeerHandle handle);
    Peer* find(const netsim::NodeAddress& address);
    void defer(std::function<void()> action);
    void remove_closed();

    const common::Clock& clock_;
    netsim::SimulatedNetwork& network_;
    netsim::NodeAddress address_;
    transport::PlayoutConfig playout_;
    transport::StaticKeyPair keys_;
    session::FakeUiEventSink ui_;
    HarnessMixer mixer_;
    session::FakeOutputDeviceControl outputs_;
    // 再生ストリームは playout_
    // の固定の設定で作るので、目標レイテンシーの変更は記録するだけにする。
    session::FakePlayoutControl playout_control_;
    session::FakeRendezvousClient rendezvous_;
    session::FakeSettingsStore settings_;
    session::FakeSecureRandom random_;
    common::FakeLogger logger_;
    std::unique_ptr<session::HostSession> session_;
    std::map<session::PeerHandle, std::unique_ptr<Peer>> peers_;
    session::PeerHandle next_handle_ = 1;
    // session のポートの呼び出しの中から session を呼び直さないよう、通知を溜めて step() で渡す。
    std::vector<std::function<void()>> deferred_;
    common::Clock::TimePoint next_tick_{};
    std::vector<float> scratch_;
    std::map<std::string, StripOutput> strip_outputs_;
    HostPeerStats closed_stats_;
};

}  // namespace vocroz::test_harness
