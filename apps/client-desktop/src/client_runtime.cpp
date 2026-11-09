// SPDX-License-Identifier: AGPL-3.0-only
//
// ClientRuntime の実装。

#include "client_runtime.h"

#include <utility>
#include <vector>

#include "vocroz/audio_core/gain.h"
#include "vocroz/control_protocol/events.h"

namespace vocroz::client {
namespace cp = control_protocol;

common::Result<std::unique_ptr<ClientRuntime>, ClientStartError> ClientRuntime::create(
    Ports ports, const Config& config) {
    auto keys = transport::StaticKeyPair::generate();
    if (!keys) {
        return common::make_unexpected(ClientStartError::kKeyGenerationFailed);
    }
    auto encoder = transport::OpusFrameEncoder::create(config.encoder);
    if (!encoder) {
        return common::make_unexpected(ClientStartError::kEncoderFailed);
    }
    return std::make_unique<ClientRuntime>(ConstructionKey{}, ports, config,
                                           std::move(keys).value(), std::move(encoder).value());
}

ClientRuntime::ClientRuntime(ConstructionKey /*key*/, Ports ports, const Config& config,
                             transport::StaticKeyPair keys, transport::OpusFrameEncoder encoder)
    : ports_(ports),
      keys_(std::move(keys)),
      sender_(wakeup_),
      capture_(ClientCapture::Ports{.sender = sender_,
                                    .microphones = ports.inputs,
                                    .app_audio = ports.app_audio,
                                    .logger = ports.logger},
               config.encoder, std::move(encoder)),
      ui_(ports.ui_channel, ports.logger),
      player_(ports.returns),
      return_outputs_(ports.devices, ports.outputs, player_, ports.logger),
      connection_(HostConnection::Ports{.endpoint = ports.endpoint,
                                        .router = ports.router,
                                        .inbox = ports.inbox,
                                        .path = ports.path,
                                        .sender = sender_,
                                        .environment = ports.environment,
                                        .random = ports.random,
                                        .keys = keys_,
                                        .clock = ports.clock,
                                        .logger = ports.logger,
                                        .listener = *this},
                  config.rendezvous_host, config.rendezvous_port),
      session_(session::ClientSession::Ports{.ui = ui_,
                                             .capture = capture_,
                                             .link = connection_,
                                             .outputs = return_outputs_,
                                             .rendezvous = ports.rendezvous,
                                             .settings = ports.settings,
                                             .clock = ports.clock,
                                             .logger = ports.logger}),
      sender_thread_(sender_, wakeup_) {}

ClientRuntime::~ClientRuntime() = default;

void ClientRuntime::start() {
    ui_.start(
        [this](const cp::UiToEngineMessage& message) { session_.handle_ui_message(message); });
    session_.start();
    refresh_input_devices();
    const auto now = ports_.clock.now();
    next_tick_ = now + kSessionTickInterval;
    next_meter_ = now + kMeterInterval;
}

void ClientRuntime::poll() {
    ports_.invites.drain(
        [this](const session::InviteCode& code, const InviteInbox::ResolveResult& result) {
            session_.on_invite_resolved(code, result);
        });
    connection_.poll();
    capture_.flush_params();
    if (capture_.take_device_lost()) {
        refresh_input_devices();
    }
    if (player_.take_device_lost()) {
        session_.on_output_device_lost();
    }
    if (connection_.linked() && session_.snapshot().client_state == cp::ClientState::kConnecting) {
        // 承認を受けたら送信を始められる。キャプチャと送信スレッドは常に動いていて、
        // 送信ミュートの解除だけで音が出るので、経路があればすぐに知らせる。
        session_.on_media_established();
    }
    const auto now = ports_.clock.now();
    if (now >= next_tick_) {
        session_.tick();
        next_tick_ = now + kSessionTickInterval;
    }
    if (now >= next_meter_) {
        publish_meter();
        next_meter_ = now + kMeterInterval;
    }
}

bool ClientRuntime::in_session() const {
    switch (session_.snapshot().client_state) {
        case cp::ClientState::kDisconnected:
        case cp::ClientState::kFailed:
            return false;
        case cp::ClientState::kResolving:
        case cp::ClientState::kAuthenticating:
        case cp::ClientState::kWaitingApproval:
        case cp::ClientState::kConnecting:
        case cp::ClientState::kConnected:
        case cp::ClientState::kReconnecting:
            return true;
    }
    return false;
}

void ClientRuntime::leave() {
    if (!in_session()) {
        return;
    }
    const auto snapshot = session_.snapshot();
    // UI の退出ボタンと同じコマンドを使い、退出の手順（親機への通知、接続を閉じる）を
    // session の 1 か所にまとめる。
    session_.handle_ui_message(cp::DiscreteCommand{.request_id = "shutdown",
                                                   .entity_id = snapshot.entity_id,
                                                   .expected_revision = snapshot.revision,
                                                   .body = cp::Leave{}});
    // 退出の通知は信頼制御層からすぐに送信スレッドへ渡るので、ここで送り出しておく。
    connection_.poll();
}

void ClientRuntime::refresh_input_devices() {
    auto listed = ports_.devices.list(audio_core::AudioDeviceDirection::kInput);
    if (!listed) {
        ports_.logger.log(common::LogLevel::kWarning, "client: input devices could not be listed");
        return;
    }
    std::vector<cp::InputDevice> devices;
    devices.reserve(listed.value().size());
    for (auto& info : listed.value()) {
        devices.push_back(cp::InputDevice{.id = std::move(info.id), .name = std::move(info.name)});
    }
    session_.on_input_devices_changed(std::move(devices));
}

void ClientRuntime::publish_meter() {
    const float peak = capture_.take_input_peak();
    cp::MeterLevel level;
    level.entity_id = session_.snapshot().entity_id;
    if (peak > 0.0F) {
        level.peak_db = audio_core::linear_to_db(peak);
    }
    ui_.publish(cp::Meters{.levels = {std::move(level)}});
}

void ClientRuntime::on_link_established() {
    session_.on_link_established();
}

void ClientRuntime::on_link_message(std::span<const std::uint8_t> message) {
    session_.on_link_message(message);
}

void ClientRuntime::on_link_lost() {
    session_.on_link_lost();
}

void ClientRuntime::on_link_resumed() {
    session_.on_link_resumed();
}

void ClientRuntime::on_link_failed(control_protocol::ErrorCode reason) {
    session_.on_link_failed(reason);
}

}  // namespace vocroz::client
