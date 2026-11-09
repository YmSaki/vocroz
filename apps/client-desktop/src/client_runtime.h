// SPDX-License-Identifier: AGPL-3.0-only
//
// 子機モードの OS に依存しない組み立て。ClientSession に、UI の中継（UiBridge）、キャプチャと
// 送信（ClientCapture、CaptureProcessor、PacketSender、SenderThread）、親機への接続
// （HostConnection）をつなぎ、コントロールスレッドの周期の処理（poll()）を持つ。
// OS とライブラリのアダプター（WebView、WASAPI、Winsock、WinHTTP）はコンポジションルートが作り、
// Ports で注入する。アダプターを差し替えれば、Linux の CI でも子機の動作を確かめられる。
//
// スレッド（docs/adr/0030-remote-pa-assembly.md）:
// - コントロールスレッド: start()、poll()、UI からのメッセージ。子機モードでは JUCE の
//   メッセージスレッド
// - オーディオスレッド: CaptureProcessor（エンコードまで）と、返送の出力デバイスの ReturnPlayer
// - 送信スレッド: PacketSender（暗号化と送信）
// - ネットワーク受信スレッド: ReceiveRouter と HostReceivePath（復号と返送のジッターバッファ）
#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "vocroz/audio_core/ports/audio_device_catalog.h"
#include "vocroz/common/ports/clock.h"
#include "vocroz/common/ports/logger.h"
#include "vocroz/common/result.h"
#include "vocroz/control_protocol/ports/message_channel.h"
#include "vocroz/network/ports/network_environment.h"
#include "vocroz/network/ports/udp_endpoint.h"
#include "vocroz/network/receive_router.h"
#include "vocroz/session/client_session.h"
#include "vocroz/session/ports/rendezvous_client.h"
#include "vocroz/session/ports/settings_store.h"
#include "vocroz/transport/opus_frame_encoder.h"
#include "vocroz/transport/playout_stream.h"
#include "vocroz/transport/ports/random_source.h"
#include "vocroz/transport/send_wakeup.h"
#include "vocroz/transport/static_key_pair.h"

#include "capture_processor.h"
#include "client_capture.h"
#include "host_connection.h"
#include "host_link_listener.h"
#include "host_receive_path.h"
#include "input_device_opener.h"
#include "invite_inbox.h"
#include "output_device_opener.h"
#include "packet_sender.h"
#include "peer_inbox.h"
#include "return_output_devices.h"
#include "return_player.h"
#include "sender_thread.h"
#include "ui_bridge.h"

namespace vocroz::client {

/// 子機モードを始められなかった理由。
enum class ClientStartError {
    kKeyGenerationFailed,  ///< 静的鍵を作れなかった（暗号ライブラリの初期化の失敗）
    kEncoderFailed,  ///< Opus のエンコーダーを作れなかった
};

/// 子機モードの組み立て。
/// スレッド安全性: 生成、破棄、start()、poll()、in_session()、leave()
/// はコントロールスレッドから呼ぶ。
class ClientRuntime final : public HostLinkListener {
public:
    /// 注入するアダプター。どれもこのオブジェクトより長く生存させる（AGENTS.md 4.3）。
    struct Ports {
        control_protocol::MessageChannel& ui_channel;  ///< UI への経路（webui）
        network::UdpEndpoint& endpoint;  ///< 受信を router へ渡すように作った UDP ソケット
        network::ReceiveRouter& router;     ///< endpoint の受信の振り分け
        PeerInbox& inbox;                   ///< path がハンドシェイクの応答を渡す先
        HostReceivePath& path;              ///< router が親機のパケットを渡す先
        transport::PlayoutStream& returns;  ///< path が返送を入れるジッターバッファ
        network::NetworkEnvironment& environment;
        audio_core::AudioDeviceCatalog& devices;
        InputDeviceOpener& inputs;
        InputDeviceOpener& app_audio;  ///< アプリ音声（既定の再生デバイスのループバック）を開く
        OutputDeviceOpener& outputs;  ///< 返送モニターの出力デバイスを開く
        session::RendezvousClient& rendezvous;  ///< 招待の解決（結果は invites に入る）
        InviteInbox& invites;
        session::SettingsStore& settings;
        transport::RandomSource& random;
        const common::Clock& clock;
        common::Logger& logger;
    };

    /// 子機モードの設定。
    struct Config {
        std::string rendezvous_host;           ///< UDP のランデブーに使うホスト名
        std::uint16_t rendezvous_port = 0;     ///< UDP のランデブーのポート
        transport::OpusEncoderConfig encoder;  ///< 送信のエンコーダーの設定
    };

    /// 組み立てる。送信スレッドを起動するが、キャプチャと接続は start() と UI の操作で始める。
    /// @param ports  注入するアダプター
    /// @param config 設定
    /// @return 組み立てたもの。鍵かエンコーダーを作れなければ理由
    [[nodiscard]] static common::Result<std::unique_ptr<ClientRuntime>, ClientStartError> create(
        Ports ports, const Config& config);

    /// create() だけが作れるようにするための鍵。make_unique から公開のコンストラクターを呼ぶため。
    class ConstructionKey {
        friend class ClientRuntime;
        ConstructionKey() = default;
    };

    /// create() から呼ぶ。直接は呼べない（鍵とエンコーダーの生成の失敗を create() で返すため）。
    ClientRuntime(ConstructionKey key, Ports ports, const Config& config,
                  transport::StaticKeyPair keys, transport::OpusFrameEncoder encoder);

    ~ClientRuntime() override;
    ClientRuntime(const ClientRuntime&) = delete;
    ClientRuntime& operator=(const ClientRuntime&) = delete;

    /// UI からの受信を始め、設定を読んで初期状態（送信ミュート、未接続）にし、入力デバイスの
    /// 一覧を session に渡す。1 回だけ呼ぶ。
    void start();

    /// コントロールスレッドの周期の処理。数 ms〜十数 ms ごとに呼ぶ。
    /// 招待の解決の結果、親機との通信、入力と出力のデバイスの抜去、session の時刻の処理、
    /// メーターを進める。
    void poll();

    /// 参加の手続き中か参加中（ウィンドウを閉じる前に確認が要る状態）なら true を返す。
    [[nodiscard]] bool in_session() const;

    /// 終了の前に、参加中なら退出を親機へ知らせる。ウィンドウを閉じるときに 1 回呼ぶ。
    void leave();

    // --- HostLinkListener ---
    void on_link_established() override;
    void on_link_message(std::span<const std::uint8_t> message) override;
    void on_link_lost() override;
    void on_link_resumed() override;
    void on_link_failed(control_protocol::ErrorCode reason) override;

private:
    void refresh_input_devices();
    void publish_meter();

    Ports ports_;
    transport::StaticKeyPair keys_;
    transport::SendWakeup wakeup_;
    PacketSender sender_;
    ClientCapture capture_;
    UiBridge ui_;
    // 返送の再生は、出力デバイス（return_outputs_）より先に作り、後に破棄する。
    ReturnPlayer player_;
    ReturnOutputDevices return_outputs_;
    HostConnection connection_;
    session::ClientSession session_;
    common::Clock::TimePoint next_tick_{};
    common::Clock::TimePoint next_meter_{};
    // 送信スレッドは最後に作り、最初に止める（sender_ より先に破棄する）。
    SenderThread sender_thread_;
};

}  // namespace vocroz::client
