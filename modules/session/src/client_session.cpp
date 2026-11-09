// SPDX-License-Identifier: AGPL-3.0-only
//
// 子機のセッション管理の実装。
// 状態の遷移は src/state/client_state_machine の遷移表に従い、ここでは遷移に伴う副作用だけを行う。
// revision
// はコマンドで変えた値（参加、退出、入力の選択と構成、送信ミュート、返送の出力の選択）でだけ進め、
// 接続の進行や入力の欠落のような観測値では進めない（docs/adr/0015-control-protocol-messages.md）。

#include "vocroz/session/client_session.h"

#include <algorithm>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>

#include "detail/revision_utils.h"
#include "detail/text_utils.h"
#include "detail/token_utils.h"
#include "request_deduplicator.h"
#include "session_constants.h"
#include "state/client_state_machine.h"
#include "wire/peer_message_codec.h"

namespace vocroz::session {
namespace {

namespace cp = control_protocol;
using cp::ClientState;
using cp::ErrorCode;
using state::ClientEvent;

// 送信レベルを子機の範囲（-60〜+12 dB）に収める。-∞ dB（null）は下限として扱う。
// 送信レベルは -∞ を持たない（スナップショットの send_level_db は有限の値）ため。
double clamp_send_level(cp::GainDb gain) noexcept {
    if (!gain) {
        return kMinSendLevelDb;
    }
    return std::clamp(*gain, kMinSendLevelDb, kMaxSendLevelDb);
}

// 再接続トークンを捨てる拒否の理由。ストリップが消えたか、使えなくなったことがわかる場合。
bool invalidates_token(ErrorCode reason) noexcept {
    return reason == ErrorCode::kRejected || reason == ErrorCode::kReconnectTimeout ||
           reason == ErrorCode::kSessionEnded;
}

}  // namespace

class ClientSession::Impl {
public:
    explicit Impl(Ports ports) : ports_(ports), requests_(kRememberedRequestIds) {
        snapshot_.entity_id = kClientEntityId;
        snapshot_.revision = 1;
        // 初期ミュート: マイクを選んだだけでは送信しない。
        snapshot_.send_muted = true;
    }

    void start() {
        auto loaded = ports_.settings.load_client();
        if (loaded) {
            settings_ = std::move(loaded).value();
        } else {
            log(loaded.error() == SettingsError::kNotFound ? common::LogLevel::kInfo
                                                           : common::LogLevel::kWarning,
                "子機の設定を読めなかったため、既定値で起動します");
        }
        if (!detail::is_displayable_text(settings_.display_name, kMaxDisplayNameBytes)) {
            settings_.display_name.clear();
        }
        settings_.send_level_db = clamp_send_level(settings_.send_level_db);
        snapshot_.selected_input_id = settings_.selected_input_id;
        snapshot_.send_level_db = settings_.send_level_db;
        snapshot_.input_channels = settings_.input_channels;
        ports_.capture.select_input(snapshot_.selected_input_id);
        ports_.capture.set_streams(streams());
        ports_.capture.set_send_muted(true, epoch_);
        ports_.capture.set_send_level(snapshot_.send_level_db);
        snapshot_.output_devices = ports_.outputs.list_outputs();
        if (settings_.output_device_id) {
            // 起動時に開けなくても、ほかのデバイスへは切り替えず、選び直すのを待つ。
            open_return_output(*settings_.output_device_id);
        }
    }

    // --- UI からのメッセージ ---

    void handle_ui_message(const cp::UiToEngineMessage& message) {
        std::visit(
            [this](const auto& m) {
                using M = std::decay_t<decltype(m)>;
                if constexpr (std::is_same_v<M, cp::Hello>) {
                    ports_.ui.publish(cp::ClientSnapshotEvent{snapshot_});
                } else if constexpr (std::is_same_v<M, cp::DiscreteCommand>) {
                    handle_discrete(m);
                } else {
                    static_assert(std::is_same_v<M, cp::SetGain>);
                    handle_gain(m);
                }
            },
            message);
    }

    // --- ランデブーサーバー ---

    void on_invite_resolved(const InviteCode& code,
                            const common::Result<ResolvedInvite, ErrorCode>& result) {
        if (snapshot_.client_state != ClientState::kResolving || pending_code_ != code) {
            return;  // 退出や参加し直しで不要になった要求の結果
        }
        pending_code_.reset();
        if (!result) {
            fail(result.error());
            return;
        }
        resolved_ = result.value();
        advance(ClientEvent::kResolved);
        ports_.link.connect(resolved_->endpoint);
    }

    // --- 親機との接続 ---

    void on_link_established() {
        if (!resolved_) {
            return;  // 招待を解決していなければ、申請に添える値がない
        }
        switch (snapshot_.client_state) {
            case ClientState::kAuthenticating: {
                wire::JoinRequest request;
                request.invite_token = resolved_->invite_token;
                request.reconnect_token = token_for(resolved_->endpoint.host_public_key);
                request.display_name = settings_.display_name;
                send(request);
                // 承認のときに入力の数だけストリップが作られるよう、申請に続けて申告する。
                send(wire::StreamsReport{streams()});
                return;
            }
            case ClientState::kReconnecting:
                if (auto token = token_for(resolved_->endpoint.host_public_key)) {
                    send(wire::ReconnectRequest{*token});
                }
                return;
            case ClientState::kDisconnected:
            case ClientState::kResolving:
            case ClientState::kWaitingApproval:
            case ClientState::kConnecting:
            case ClientState::kConnected:
            case ClientState::kFailed:
                return;
        }
    }

    void on_link_message(std::span<const std::uint8_t> bytes) {
        auto decoded = wire::decode_host_to_client(bytes);
        if (!decoded) {
            log(common::LogLevel::kWarning, "親機からの不正な制御メッセージを捨てました");
            return;
        }
        std::visit(
            [this](const auto& m) {
                using M = std::decay_t<decltype(m)>;
                if constexpr (std::is_same_v<M, wire::JoinPending>) {
                    if (advance(ClientEvent::kJoinPending)) {
                        snapshot_.session_name = m.session_name;
                        publish();
                    }
                } else if constexpr (std::is_same_v<M, wire::JoinApproved>) {
                    handle_approved(m.reconnect_token);
                } else if constexpr (std::is_same_v<M, wire::JoinDenied>) {
                    if (!in_session()) {
                        return;
                    }
                    if (invalidates_token(m.reason)) {
                        reconnect_token_.reset();
                    }
                    fail(m.reason);
                    ports_.link.close();
                } else if constexpr (std::is_same_v<M, wire::SessionEndedNotice>) {
                    handle_session_ended();
                } else {
                    static_assert(std::is_same_v<M, wire::ReturnMuteNotice>);
                    handle_return_mute(m);
                }
            },
            decoded.value());
    }

    void on_link_lost() {
        switch (snapshot_.client_state) {
            case ClientState::kConnecting:
            case ClientState::kConnected:
                advance(ClientEvent::kLinkLost);
                reconnecting_since_ = ports_.clock.now();
                ports_.link.reconnect();
                return;
            case ClientState::kAuthenticating:
            case ClientState::kWaitingApproval:
                // 承認前は再接続トークンがなく、同じ申請に戻る手段がないので、失敗として終える。
                fail(ErrorCode::kUnreachable);
                ports_.link.close();
                return;
            case ClientState::kDisconnected:
            case ClientState::kResolving:
            case ClientState::kReconnecting:
            case ClientState::kFailed:
                return;
        }
    }

    void on_link_resumed() {
        if (advance(ClientEvent::kReconnected)) {
            reconnecting_since_.reset();
            report_state();
        }
    }

    void on_link_failed(ErrorCode reason) {
        if (!in_session() && snapshot_.client_state != ClientState::kResolving) {
            return;
        }
        fail(reason);
        ports_.link.close();
    }

    void on_media_established() {
        advance(ClientEvent::kMediaEstablished);
    }

    void on_input_devices_changed(std::vector<cp::InputDevice> devices) {
        snapshot_.input_devices = std::move(devices);
        const bool missing = selected_device_missing();
        const bool newly_missing = missing && !snapshot_.input_missing;
        const bool changed = missing != snapshot_.input_missing;
        snapshot_.input_missing = missing;
        publish();
        if (newly_missing) {
            // 送信ミュートは変えず、無音を送り続ける（親機では信号なしとして見える）。
            notice(cp::NoticeSeverity::kWarning, "input_missing");
        }
        if (changed && link_usable()) {
            send(wire::InputReport{missing});
        }
    }

    void on_output_device_lost() {
        // 抜けたデバイスの後始末。閉じるのは冪等なので、状態にかかわらず閉じておく。
        ports_.outputs.close_output();
        snapshot_.output_devices = ports_.outputs.list_outputs();
        if (!snapshot_.selected_output_id || snapshot_.output_missing) {
            publish();
            return;
        }
        // ID は残し、挿し直したデバイスを選び直しやすくする。観測値なので revision は進めない。
        snapshot_.output_missing = true;
        publish();
        notice(cp::NoticeSeverity::kWarning, "return_output_lost");
    }

    void tick() {
        if (snapshot_.client_state == ClientState::kReconnecting && reconnecting_since_ &&
            ports_.clock.now() - *reconnecting_since_ >= kReconnectGrace) {
            // 親機のストリップは LOST で残るので、再接続トークンは捨てない。
            // 同じ招待で参加し直せば、承認なしで同じストリップへ戻れる。
            fail(ErrorCode::kReconnectTimeout);
            ports_.link.close();
        }
    }

    [[nodiscard]] const cp::ClientSnapshot& snapshot() const noexcept {
        return snapshot_;
    }

private:
    using CommandResult = common::Result<cp::Revision, ErrorCode>;

    // 申請済みの親機の再接続トークン。別の親機に参加するときは添えない。
    struct HostToken {
        HostPublicKey host{};
        ReconnectToken token{};
    };

    // --- 離散コマンド ---

    void handle_discrete(const cp::DiscreteCommand& command) {
        if (!requests_.remember(command.request_id)) {
            return;
        }
        const auto result = apply(command);
        if (result) {
            ports_.ui.publish(cp::Ack{command.request_id, result.value()});
        } else {
            ports_.ui.publish(cp::Reject{command.request_id, result.error()});
        }
    }

    [[nodiscard]] CommandResult apply(const cp::DiscreteCommand& command) {
        if (command.entity_id != kClientEntityId) {
            return common::make_unexpected(ErrorCode::kNotFound);
        }
        if (command.expected_revision != snapshot_.revision) {
            return common::make_unexpected(ErrorCode::kRevisionConflict);
        }
        // 認可: 子機で受け付けるのは子機のコマンドだけ。親機のミキサーのコマンドは拒否する。
        return std::visit(
            [this](const auto& body) -> CommandResult {
                using B = std::decay_t<decltype(body)>;
                if constexpr (std::is_same_v<B, cp::Join>) {
                    return join(body.invite);
                } else if constexpr (std::is_same_v<B, cp::Leave>) {
                    return leave();
                } else if constexpr (std::is_same_v<B, cp::SelectInput>) {
                    return select_input(body.device_id);
                } else if constexpr (std::is_same_v<B, cp::SetSendMute>) {
                    return set_send_mute(body.muted);
                } else if constexpr (std::is_same_v<B, cp::SetInputChannels>) {
                    return set_input_channels(body.channels);
                } else if constexpr (std::is_same_v<B, cp::SetAppAudio>) {
                    return set_app_audio(body.enabled);
                } else if constexpr (std::is_same_v<B, cp::SelectOutput>) {
                    return select_output(body.device_id);
                } else {
                    return common::make_unexpected(ErrorCode::kInvalidState);
                }
            },
            command.body);
    }

    CommandResult join(const std::string& invite) {
        if (!state::next_state(snapshot_.client_state, ClientEvent::kJoin)) {
            return common::make_unexpected(ErrorCode::kInvalidState);
        }
        auto code = parse_invite(invite);
        if (!code) {
            return common::make_unexpected(ErrorCode::kInviteInvalid);
        }
        advance(ClientEvent::kJoin, false);
        snapshot_.failure_code.reset();
        snapshot_.session_name.reset();
        // 初期ミュート: 新しく参加するときは、送信ミュートから始める。
        force_send_mute();
        reset_return();
        resolved_.reset();
        pending_code_ = code.value();
        detail::bump_revision(snapshot_.revision);
        publish();
        ports_.rendezvous.resolve_invite(code.value());
        return snapshot_.revision;
    }

    CommandResult leave() {
        if (in_session()) {
            send(wire::LeaveNotice{});
        }
        if (snapshot_.client_state != ClientState::kDisconnected &&
            snapshot_.client_state != ClientState::kFailed) {
            ports_.link.close();
        }
        advance(ClientEvent::kLeave, false);
        snapshot_.failure_code.reset();
        snapshot_.session_name.reset();
        force_send_mute();
        reset_return();
        pending_code_.reset();
        resolved_.reset();
        reconnecting_since_.reset();
        detail::bump_revision(snapshot_.revision);
        publish();
        save_settings();
        return snapshot_.revision;
    }

    CommandResult select_input(const std::optional<std::string>& device_id) {
        snapshot_.selected_input_id = device_id;
        settings_.selected_input_id = device_id;
        ports_.capture.select_input(device_id);
        const bool missing = selected_device_missing();
        const bool changed = missing != snapshot_.input_missing;
        snapshot_.input_missing = missing;
        detail::bump_revision(snapshot_.revision);
        publish();
        if (changed && link_usable()) {
            send(wire::InputReport{missing});
        }
        save_settings();
        return snapshot_.revision;
    }

    CommandResult select_output(const std::optional<std::string>& device_id) {
        snapshot_.output_devices = ports_.outputs.list_outputs();
        if (snapshot_.selected_output_id == device_id && !snapshot_.output_missing) {
            // 開き直すと返送が途切れるので何もしない。一覧だけは新しくなっているので知らせる。
            publish();
            return snapshot_.revision;
        }
        bool opened = true;
        if (device_id) {
            opened = open_return_output(*device_id);
        } else {
            ports_.outputs.close_output();
            snapshot_.selected_output_id.reset();
            snapshot_.output_missing = false;
        }
        if (opened) {
            settings_.output_device_id = device_id;
            save_settings();
        }
        // 開けなかったときも、前のデバイスは閉じて状態が変わっているので revision
        // を進めて知らせる。
        detail::bump_revision(snapshot_.revision);
        publish();
        if (!opened) {
            log(common::LogLevel::kWarning, "返送の出力デバイスを開けませんでした");
            return common::make_unexpected(ErrorCode::kDeviceUnavailable);
        }
        return snapshot_.revision;
    }

    // 返送の出力デバイスを開く。2 つのデバイスから同時に返送を鳴らさないよう、必ず閉じてから開く。
    // 開けなければ、選んだ ID を残して「出力なし」の表示にする（ほかのデバイスへは切り替えない）。
    bool open_return_output(const std::string& device_id) {
        ports_.outputs.close_output();
        snapshot_.selected_output_id = device_id;
        const bool opened = ports_.outputs.open_output(device_id).has_value();
        snapshot_.output_missing = !opened;
        return opened;
    }

    CommandResult set_send_mute(bool muted) {
        if (snapshot_.send_muted != muted) {
            if (!muted) {
                // 解除のたびに世代番号を進め、親機がミュート前の古いフレームを捨てられるようにする。
                ++epoch_;
            }
            snapshot_.send_muted = muted;
            ports_.capture.set_send_muted(muted, epoch_);
            if (link_usable()) {
                send(wire::SendMuteReport{muted, epoch_});
            }
            detail::bump_revision(snapshot_.revision);
            publish();
        }
        return snapshot_.revision;
    }

    CommandResult set_input_channels(std::uint8_t channels) {
        if (snapshot_.input_channels != channels) {
            snapshot_.input_channels = channels;
            settings_.input_channels = channels;
            apply_streams();
            save_settings();
        }
        return snapshot_.revision;
    }

    CommandResult set_app_audio(bool enabled) {
        if (snapshot_.app_audio != enabled) {
            snapshot_.app_audio = enabled;
            apply_streams();
        }
        return snapshot_.revision;
    }

    // 入力の構成をキャプチャへ反映し、親機へ申告し直す。親機は変わった入力のストリップを
    // 作り直す（チャンネルミュートで始まる）ので、送信ミュートはここでは変えない。
    void apply_streams() {
        const auto list = streams();
        ports_.capture.set_streams(list);
        if (in_session()) {
            send(wire::StreamsReport{list});
        }
        detail::bump_revision(snapshot_.revision);
        publish();
    }

    // 送る入力の一覧。マイクはストリーム 0、アプリ音声はストリーム 1 にする。
    // マイクのストリーム ID は、申告を送らない子機（親機はストリーム 0 のモノラルとみなす）と
    // 同じにしておく。
    [[nodiscard]] std::vector<InputStream> streams() const {
        std::vector<InputStream> list{
            InputStream{kMicrophoneStreamId, cp::InputSourceKind::kMicrophone,
                        snapshot_.input_channels},
        };
        if (snapshot_.app_audio) {
            list.push_back(InputStream{kAppAudioStreamId, cp::InputSourceKind::kApplicationAudio,
                                       kAppAudioChannels});
        }
        return list;
    }

    // --- 連続値のコマンド ---

    void handle_gain(const cp::SetGain& command) {
        if (command.entity_id != kClientEntityId) {
            return;
        }
        if (last_gain_seq_ && command.seq < *last_gain_seq_) {
            return;
        }
        last_gain_seq_ = command.seq;
        snapshot_.send_level_db = clamp_send_level(command.gain_db);
        settings_.send_level_db = snapshot_.send_level_db;
        ports_.capture.set_send_level(snapshot_.send_level_db);
        publish();
        ports_.ui.publish(cp::GainAck{command.entity_id, command.seq, snapshot_.revision});
    }

    // --- 親機からの制御メッセージ ---

    void handle_approved(const ReconnectToken& token) {
        if (!resolved_) {
            return;
        }
        reconnect_token_ = HostToken{resolved_->endpoint.host_public_key, token};
        switch (snapshot_.client_state) {
            case ClientState::kAuthenticating:
            case ClientState::kWaitingApproval:
                advance(ClientEvent::kApproved);
                report_state();
                return;
            case ClientState::kReconnecting:
                advance(ClientEvent::kReconnected);
                reconnecting_since_.reset();
                report_state();
                return;
            case ClientState::kDisconnected:
            case ClientState::kResolving:
            case ClientState::kConnecting:
            case ClientState::kConnected:
            case ClientState::kFailed:
                return;
        }
    }

    // 返送のミュートは親機だけが決める。子機は受信側の判定へ渡して表示に写すだけで、
    // 観測値なので revision は進めない。
    void handle_return_mute(const wire::ReturnMuteNotice& notice) {
        if (!in_session()) {
            return;
        }
        ports_.link.apply_return_mute(notice.muted, notice.epoch);
        if (snapshot_.return_muted != notice.muted) {
            snapshot_.return_muted = notice.muted;
            publish();
        }
    }

    void handle_session_ended() {
        if (!advance(ClientEvent::kSessionEnded, false)) {
            return;
        }
        reconnect_token_.reset();
        snapshot_.failure_code = ErrorCode::kSessionEnded;
        force_send_mute();
        reset_return();
        reconnecting_since_.reset();
        ports_.link.close();
        publish();
        save_settings();
    }

    // 承認後と再接続後に、親機へ今の送信ミュートと入力の状態を伝える。
    // 親機は新しいストリップを送信ミュートとして扱うので、解除済みならここで知らせる。
    void report_state() {
        send(wire::SendMuteReport{snapshot_.send_muted, epoch_});
        send(wire::InputReport{snapshot_.input_missing});
        send(wire::StreamsReport{streams()});
    }

    // --- 状態 ---

    // 状態を進め、必要なら UI に知らせる。接続の進行は観測値なので revision は進めない。
    bool advance(ClientEvent event, bool publish_now = true) {
        const auto next = state::next_state(snapshot_.client_state, event);
        if (!next) {
            return false;
        }
        snapshot_.client_state = *next;
        if (publish_now) {
            publish();
        }
        return true;
    }

    void fail(ErrorCode reason) {
        if (!advance(ClientEvent::kFailed, false)) {
            return;
        }
        snapshot_.failure_code = reason;
        reset_return();
        reconnecting_since_.reset();
        publish();
    }

    // 親機との接続がなくなったら、返送は届かないのでミュート（OFF）の表示に戻す。
    // 次の接続では、親機が承認の後に今の状態を知らせてくる。
    void reset_return() {
        snapshot_.return_muted = true;
    }

    void force_send_mute() {
        if (!snapshot_.send_muted) {
            snapshot_.send_muted = true;
            ports_.capture.set_send_muted(true, epoch_);
        }
    }

    // 親機との接続の上で制御メッセージをやり取りしている状態（承認の前後、再接続中を含む）。
    [[nodiscard]] bool in_session() const noexcept {
        switch (snapshot_.client_state) {
            case ClientState::kAuthenticating:
            case ClientState::kWaitingApproval:
            case ClientState::kConnecting:
            case ClientState::kConnected:
            case ClientState::kReconnecting:
                return true;
            case ClientState::kDisconnected:
            case ClientState::kResolving:
            case ClientState::kFailed:
                return false;
        }
        return false;
    }

    // 親機にストリップがあり、状態の通知が届く状態。承認前の通知は親機が使わないので送らない。
    [[nodiscard]] bool link_usable() const noexcept {
        return snapshot_.client_state == ClientState::kConnecting ||
               snapshot_.client_state == ClientState::kConnected;
    }

    [[nodiscard]] bool selected_device_missing() const {
        if (!snapshot_.selected_input_id) {
            return false;  // 「入力なし」を選んだのは出演者の意思なので、欠落とは扱わない
        }
        return std::ranges::none_of(snapshot_.input_devices, [&](const cp::InputDevice& d) {
            return d.id == *snapshot_.selected_input_id;
        });
    }

    [[nodiscard]] std::optional<ReconnectToken> token_for(const HostPublicKey& host) const {
        if (reconnect_token_ && detail::constant_time_equal(reconnect_token_->host, host)) {
            return reconnect_token_->token;
        }
        return std::nullopt;
    }

    void send(const wire::ClientToHostMessage& message) {
        const auto bytes = wire::encode(message);
        ports_.link.send(bytes);
    }

    void save_settings() {
        if (const auto saved = ports_.settings.save_client(settings_); !saved) {
            log(common::LogLevel::kWarning, "子機の設定を保存できませんでした");
        }
    }

    void publish() {
        ports_.ui.publish(cp::ClientUpdated{snapshot_});
    }

    void notice(cp::NoticeSeverity severity, const char* code) {
        cp::Notice n;
        n.severity = severity;
        n.code = code;
        ports_.ui.publish(n);
    }

    void log(common::LogLevel level, const char* message) {
        ports_.logger.log(level, message);
    }

    Ports ports_;
    ClientSettings settings_;
    cp::ClientSnapshot snapshot_;
    // 送信ミュートの世代番号。0 は起動時のミュートの世代。
    std::uint32_t epoch_ = 0;
    std::optional<cp::Sequence> last_gain_seq_;
    std::optional<InviteCode> pending_code_;
    std::optional<ResolvedInvite> resolved_;
    // 再接続トークンはメモリにだけ置き、設定ファイルやログに書かない。
    std::optional<HostToken> reconnect_token_;
    std::optional<common::Clock::TimePoint> reconnecting_since_;
    RequestDeduplicator requests_;
};

ClientSession::ClientSession(Ports ports) : impl_(std::make_unique<Impl>(ports)) {}

ClientSession::~ClientSession() = default;

void ClientSession::start() {
    impl_->start();
}

void ClientSession::handle_ui_message(const control_protocol::UiToEngineMessage& message) {
    impl_->handle_ui_message(message);
}

void ClientSession::on_invite_resolved(
    const InviteCode& code,
    const common::Result<ResolvedInvite, control_protocol::ErrorCode>& result) {
    impl_->on_invite_resolved(code, result);
}

void ClientSession::on_link_established() {
    impl_->on_link_established();
}

void ClientSession::on_link_message(std::span<const std::uint8_t> message) {
    impl_->on_link_message(message);
}

void ClientSession::on_link_lost() {
    impl_->on_link_lost();
}

void ClientSession::on_link_resumed() {
    impl_->on_link_resumed();
}

void ClientSession::on_link_failed(control_protocol::ErrorCode reason) {
    impl_->on_link_failed(reason);
}

void ClientSession::on_media_established() {
    impl_->on_media_established();
}

void ClientSession::on_input_devices_changed(std::vector<control_protocol::InputDevice> devices) {
    impl_->on_input_devices_changed(std::move(devices));
}

void ClientSession::on_output_device_lost() {
    impl_->on_output_device_lost();
}

void ClientSession::tick() {
    impl_->tick();
}

control_protocol::ClientSnapshot ClientSession::snapshot() const {
    return impl_->snapshot();
}

}  // namespace vocroz::session
