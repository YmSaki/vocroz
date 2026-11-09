// SPDX-License-Identifier: AGPL-3.0-only
//
// 結合テストの子機（remote-pa の子機モードの代わり）。
// session の ClientSession を、transport の部品（Noise_IK の開始側、暗号化、信頼制御層、死活監視、
// Opus のエンコード）とシミュレーションのネットワークにつなぐ。
// HostLink ポートで接続を張り、CaptureControl
// ポートの送信ミュートが解けている間だけメディアを送る。 UI、ランデブーサーバー、設定は session
// のフェイクを使い、テストから直接操作する。
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "vocroz/common/ports/clock.h"
#include "vocroz/netsim/simulated_network.h"
#include "vocroz/session/client_session.h"
#include "vocroz/session/ports/capture_control.h"
#include "vocroz/session/ports/host_link.h"
#include "vocroz/transport/noise_ik_handshake.h"
#include "vocroz/transport/opus_frame_encoder.h"
#include "vocroz/transport/ports/control_message_listener.h"
#include "vocroz/transport/ports/liveness_listener.h"
#include "vocroz/transport/static_key_pair.h"

#include "fakes/fake_logger.h"
#include "fakes/fake_rendezvous_client.h"
#include "fakes/fake_settings_store.h"
#include "fakes/fake_ui_event_sink.h"

namespace vocroz::test_harness {

/// 子機の設定。
struct SimulatedClientConfig {
    /// 親機の承認パネルとストリップに出す名前。
    std::string display_name;
    /// エンコーダーの設定。
    transport::OpusEncoderConfig encoder;
    /// true なら送信ミュートと承認を無視してメディアを送り続ける（承認前の遮断を確かめる、
    /// 製品の振る舞いに従わない子機）。
    bool ignore_send_mute = false;
};

/// 子機の統計。
struct SimulatedClientStats {
    /// 送ったメディアフレーム。
    std::uint64_t media_frames_sent = 0;
    /// 張った暗号化セッションの数（再接続のたびに増える）。
    std::uint64_t links_established = 0;
};

/// 結合テストの子機。
/// スレッド安全性: 1 つのスレッドから使う（テストでは仮想時刻を進めるスレッドが、
/// コントロールスレッド、ネットワーク受信スレッド、オーディオスレッドの役をすべてする）。
class SimulatedClient final : private session::CaptureControl,
                              private session::HostLink,
                              private transport::ControlMessageListener,
                              private transport::LivenessListener {
public:
    /// @param clock   時刻の取得元。このオブジェクトより長く生存させる
    /// @param network ネットワーク。このオブジェクトより長く生存させる
    /// @param address 子機のアドレス
    /// @param host    親機のアドレス（ホールパンチを済ませた後の経路とみなす）
    /// @param config  子機の設定
    SimulatedClient(const common::Clock& clock, netsim::SimulatedNetwork& network,
                    const netsim::NodeAddress& address, const netsim::NodeAddress& host,
                    SimulatedClientConfig config);
    SimulatedClient(const SimulatedClient&) = delete;
    SimulatedClient& operator=(const SimulatedClient&) = delete;
    SimulatedClient(SimulatedClient&&) = delete;
    SimulatedClient& operator=(SimulatedClient&&) = delete;
    ~SimulatedClient() override;

    /// 子機のアドレス。
    [[nodiscard]] const netsim::NodeAddress& address() const noexcept {
        return address_;
    }

    /// セッション管理。UI のコマンドとランデブーサーバーの結果は、テストがここへ直接渡す。
    [[nodiscard]] session::ClientSession& session() noexcept {
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

    /// キャプチャのコールバック（1 ブロック分のマイクの PCM）。
    /// 接続があり、送信ミュートが解けていればエンコードして送る。
    /// @param block キャプチャのブロック。マイクをステレオにしていれば L、R のインターリーブ
    void capture(std::span<const float> block);

    /// アプリ音声のキャプチャのコールバック（1 ブロック分のステレオの PCM、インターリーブ）。
    /// アプリ音声を送る構成で、接続があり、送信ミュートが解けていればエンコードして送る。
    /// @param block キャプチャのブロック
    void capture_app_audio(std::span<const float> block);

    /// 時間を進めたあとの処理（ハンドシェイクの再送、信頼制御層の再送とキープアライブ、死活監視、
    /// 接続の張り直し、session の tick）。ネットワークの配送のあとに呼ぶ。
    void step();

    /// 統計。
    [[nodiscard]] const SimulatedClientStats& stats() const noexcept {
        return stats_;
    }

private:
    struct Link;

    // session::CaptureControl
    void select_input(const std::optional<std::string>& device_id) override;
    void set_send_muted(bool muted, std::uint32_t epoch) override;
    void set_send_level(double level_db) override;
    void set_streams(const std::vector<session::InputStream>& streams) override;

    void capture_input(control_protocol::InputSourceKind kind, std::span<const float> block);

    // session::HostLink
    void connect(const session::HostEndpoint& endpoint) override;
    void reconnect() override;
    void send(std::span<const std::uint8_t> message) override;
    void apply_return_mute(bool muted, std::uint32_t epoch) override;
    void close() override;

    // transport::ControlMessageListener
    void on_control_message(transport::ControlLane lane,
                            std::span<const std::uint8_t> message) override;

    // transport::LivenessListener
    void on_peer_timed_out() override;
    void on_peer_resumed() override;

    void on_datagram(const netsim::NodeAddress& from, std::span<const std::uint8_t> datagram);
    void on_handshake_response(std::span<const std::uint8_t> datagram);
    void start_handshake();
    void apply_link_request();
    void defer(std::function<void()> action);

    // 接続の張り直しと切断の要求。session のポートの呼び出しは接続の受信の処理の中から来るので、
    // その場で接続を破棄せず、step() でまとめて反映する。
    enum class LinkRequest : std::uint8_t { kNone, kConnect, kClose };

    const common::Clock& clock_;
    netsim::SimulatedNetwork& network_;
    netsim::NodeAddress address_;
    netsim::NodeAddress host_;
    SimulatedClientConfig config_;
    transport::StaticKeyPair keys_;
    session::FakeUiEventSink ui_;
    session::FakeRendezvousClient rendezvous_;
    session::FakeSettingsStore settings_;
    common::FakeLogger logger_;
    std::unique_ptr<session::ClientSession> session_;
    std::optional<transport::PublicKey> host_public_key_;
    // 応答を待っているハンドシェイク。応答が来るまで同じ 1 つ目を再送する。
    std::optional<transport::NoiseIkHandshake> handshake_;
    std::vector<std::uint8_t> initiation_;
    common::Clock::TimePoint next_initiation_{};
    std::unique_ptr<Link> link_;
    LinkRequest link_request_ = LinkRequest::kNone;
    bool send_muted_ = true;
    // session が反映した入力の構成。session の start() で上書きされる。
    std::vector<session::InputStream> streams_{session::InputStream{}};
    std::uint32_t mute_epoch_ = 0;
    std::vector<std::function<void()>> deferred_;
    common::Clock::TimePoint next_tick_{};
    SimulatedClientStats stats_;
};

}  // namespace vocroz::test_harness
