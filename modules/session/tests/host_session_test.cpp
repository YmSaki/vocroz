// SPDX-License-Identifier: AGPL-3.0-only
//
// 親機のセッション管理のテスト。
// 初期ミュート、承認前の遮断、切断からの同じストリップへの復帰、招待と権限の検証、
// コマンドの revision の規則を、ポートのフェイクで OS なしに確かめる。

#include "vocroz/session/host_session.h"

#include <algorithm>
#include <chrono>
#include <optional>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "fakes/fake_clock.h"
#include "fakes/fake_external_output_control.h"
#include "fakes/fake_logger.h"
#include "fakes/fake_mixer_control.h"
#include "fakes/fake_output_device_control.h"
#include "fakes/fake_peer_link.h"
#include "fakes/fake_playout_control.h"
#include "fakes/fake_plugin_processor.h"
#include "fakes/fake_plugin_rack.h"
#include "fakes/fake_plugin_scan_control.h"
#include "fakes/fake_rendezvous_client.h"
#include "fakes/fake_secret_store.h"
#include "fakes/fake_secure_random.h"
#include "fakes/fake_settings_store.h"
#include "fakes/fake_ui_event_sink.h"
#include "session_constants.h"
#include "wire/peer_message_codec.h"

namespace vocroz::session {
namespace {

namespace cp = control_protocol;
using cp::ConnectionState;
using cp::ErrorCode;
using cp::SessionState;
using namespace std::chrono_literals;

class HostSessionTest : public ::testing::Test {
protected:
    HostSessionTest() {
        config_.invite_base_url = "https://vocroz.example";
        config_.host_public_key.fill(0xAB);
    }

    HostSession& session() {
        if (!session_) {
            session_.emplace(
                config_, HostSession::Ports{ui_, mixer_, outputs_, peers_, rendezvous_, settings_,
                                            random_, clock_, logger_, playout_, external_output_,
                                            secrets_, plugins_, plugin_scan_});
            session_->start();
        }
        return *session_;
    }

    // 離散コマンドを送り、応答（Ack なら revision、Reject なら原因）を返す。
    std::optional<cp::Revision> command(const std::string& entity_id, cp::DiscreteCommandBody body,
                                        std::optional<cp::Revision> expected = std::nullopt) {
        const auto request_id = "r" + std::to_string(++request_number_);
        const auto revision = expected ? *expected : revision_of(entity_id);
        session().handle_ui_message(cp::DiscreteCommand{request_id, entity_id, revision, body});
        last_reject_.reset();
        for (auto it = ui_.messages().rbegin(); it != ui_.messages().rend(); ++it) {
            if (const auto* ack = std::get_if<cp::Ack>(&*it);
                ack && ack->request_id == request_id) {
                return ack->applied_revision;
            }
            if (const auto* rej = std::get_if<cp::Reject>(&*it);
                rej && rej->request_id == request_id) {
                last_reject_ = rej->error_code;
                return std::nullopt;
            }
        }
        ADD_FAILURE() << "応答がない: " << request_id;
        return std::nullopt;
    }

    cp::Revision revision_of(const std::string& entity_id) {
        const auto s = session().snapshot();
        if (entity_id == "session") {
            return s.session.revision;
        }
        if (entity_id == "master") {
            return s.master.revision;
        }
        for (const auto& c : s.channels) {
            if (c.entity_id == entity_id) {
                return c.revision;
            }
        }
        for (const auto& p : s.pending) {
            if (p.entity_id == entity_id) {
                return p.revision;
            }
        }
        if (entity_id == "routing") {
            return s.routing.revision;
        }
        if (entity_id == "plugins") {
            // プラグインの状態はスナップショットに入らないので、最後に届いたものから読む。
            const auto plugins = ui_.last<cp::PluginsUpdated>();
            return plugins ? plugins->plugins.revision : 0;
        }
        for (const auto& level : s.routing.levels) {
            if (level.entity_id == entity_id) {
                return level.revision;
            }
        }
        return 0;
    }

    // セッションを作り、招待の登録を成功させる。
    void create() {
        ASSERT_TRUE(command("session", cp::CreateSession{}));
        register_latest_invite("ABCDE12345");
    }

    void register_latest_invite(std::string_view code) {
        ASSERT_FALSE(rendezvous_.registrations().empty());
        RevokeKey key{};
        key.fill(0x5A);
        session().on_invite_registered(rendezvous_.registrations().back().invite_token,
                                       RegisteredInvite{parse_invite_code(code).value(), key});
    }

    InviteToken current_invite_token() const {
        return rendezvous_.registrations().back().invite_token;
    }

    void send_from(PeerHandle peer, const wire::ClientToHostMessage& message) {
        session().on_peer_message(peer, wire::encode(message));
    }

    void request_join(PeerHandle peer, const std::string& name,
                      std::optional<ReconnectToken> token = std::nullopt) {
        send_from(peer, wire::JoinRequest{current_invite_token(), token, name});
    }

    // 親機が接続 peer に送った最後のメッセージ。
    std::optional<wire::HostToClientMessage> last_sent(PeerHandle peer) const {
        const auto sent = peers_.sent_to(peer);
        if (sent.empty()) {
            return std::nullopt;
        }
        auto decoded = wire::decode_host_to_client(sent.back());
        EXPECT_TRUE(decoded);
        return decoded ? std::optional{decoded.value()} : std::nullopt;
    }

    std::optional<ReconnectToken> approved_token(PeerHandle peer) const {
        const auto m = last_sent(peer);
        if (!m) {
            return std::nullopt;
        }
        const auto* approved = std::get_if<wire::JoinApproved>(&*m);
        return approved ? std::optional{approved->reconnect_token} : std::nullopt;
    }

    std::optional<ErrorCode> denied_reason(PeerHandle peer) const {
        const auto m = last_sent(peer);
        if (!m) {
            return std::nullopt;
        }
        const auto* denied = std::get_if<wire::JoinDenied>(&*m);
        return denied ? std::optional{denied->reason} : std::nullopt;
    }

    // 申請から承認、経路の確立までを進め、ストリップの entity_id を返す。
    std::string connect(PeerHandle peer, const std::string& name) {
        request_join(peer, name);
        const auto pending = session().snapshot().pending;
        EXPECT_FALSE(pending.empty());
        const auto pending_id = pending.back().entity_id;
        EXPECT_TRUE(command(pending_id, cp::ApproveParticipant{}));
        session().on_peer_established(peer);
        return session().snapshot().channels.back().entity_id;
    }

    cp::ChannelStrip strip(const std::string& entity_id) {
        for (const auto& c : session().snapshot().channels) {
            if (c.entity_id == entity_id) {
                return c;
            }
        }
        ADD_FAILURE() << "ストリップがない: " << entity_id;
        return {};
    }

    HostSession::Config config_;
    FakeUiEventSink ui_;
    FakeMixerControl mixer_;
    FakeOutputDeviceControl outputs_;
    FakePlayoutControl playout_;
    FakePeerLink peers_;
    FakeRendezvousClient rendezvous_;
    FakeSettingsStore settings_;
    FakeSecureRandom random_;
    FakeExternalOutputControl external_output_;
    FakeSecretStore secrets_;
    FakePluginRack plugins_;
    FakePluginScanControl plugin_scan_;
    common::FakeClock clock_;
    common::FakeLogger logger_;
    std::optional<HostSession> session_;
    int request_number_ = 0;
    std::optional<ErrorCode> last_reject_;
};

// --- 初期ミュートと設定 ---

TEST_F(HostSessionTest, StartsWithMasterMutedAndNoSession) {
    session();
    EXPECT_TRUE(mixer_.master_muted());
    const auto s = session().snapshot();
    EXPECT_EQ(s.session.state, SessionState::kIdle);
    EXPECT_TRUE(s.master.master_muted);
    EXPECT_TRUE(s.channels.empty());
}

TEST_F(HostSessionTest, StartsMutedWithDefaultsWhenSettingsAreCorrupted) {
    settings_.fail_loads_with(SettingsError::kCorrupted);
    session();
    EXPECT_TRUE(mixer_.master_muted());
    EXPECT_EQ(mixer_.master_gain_db(), cp::GainDb{0.0});
    ASSERT_FALSE(logger_.entries().empty());
    EXPECT_EQ(logger_.entries().front().level, common::LogLevel::kWarning);
}

TEST_F(HostSessionTest, RestoresTheMasterFaderButNeverTheMute) {
    settings_.host() = HostSettings{"日曜リハーサル", "dev-1", -6.0, {}, {}};
    session();
    EXPECT_TRUE(mixer_.master_muted());
    EXPECT_EQ(mixer_.master_gain_db(), cp::GainDb{-6.0});
    EXPECT_EQ(session().snapshot().master.gain_db, cp::GainDb{-6.0});
}

// --- セッションの作成と招待 ---

TEST_F(HostSessionTest, CreatingASessionRegistersAnInviteAndBecomesReady) {
    settings_.host() = HostSettings{"日曜リハーサル", std::nullopt, 0.0, {}, {}};
    ASSERT_TRUE(command("session", cp::CreateSession{}));
    EXPECT_EQ(session().snapshot().session.state, SessionState::kCreating);
    ASSERT_EQ(rendezvous_.registrations().size(), 1U);
    EXPECT_EQ(rendezvous_.registrations().front().host_public_key, config_.host_public_key);

    register_latest_invite("ABCDE12345");
    const auto info = session().snapshot().session;
    EXPECT_EQ(info.state, SessionState::kReady);
    EXPECT_EQ(info.name, "日曜リハーサル");
    EXPECT_EQ(info.invite_url, "https://vocroz.example/join#ABCDE-12345");
}

TEST_F(HostSessionTest, RendezvousFailureMovesTheSessionToErrorWithTheCause) {
    ASSERT_TRUE(command("session", cp::CreateSession{}));
    session().on_invite_registered(current_invite_token(),
                                   common::make_unexpected(ErrorCode::kUnreachable));
    const auto info = session().snapshot().session;
    EXPECT_EQ(info.state, SessionState::kError);
    EXPECT_EQ(info.error_code, ErrorCode::kUnreachable);
    // エラーを確認すると IDLE に戻る。
    ASSERT_TRUE(command("session", cp::EndSession{}));
    EXPECT_EQ(session().snapshot().session.state, SessionState::kIdle);
}

TEST_F(HostSessionTest, RejectsCreatingWhileASessionExists) {
    create();
    EXPECT_FALSE(command("session", cp::CreateSession{}));
    EXPECT_EQ(last_reject_, ErrorCode::kInvalidState);
}

TEST_F(HostSessionTest, ReissuesTheInviteWhenItExpires) {
    create();
    clock_.advance(30min);
    session().tick();
    EXPECT_EQ(session().snapshot().session.invite_url, std::nullopt);
    ASSERT_EQ(rendezvous_.registrations().size(), 2U);
    register_latest_invite("FGHJK67890");
    EXPECT_EQ(session().snapshot().session.invite_url, "https://vocroz.example/join#FGHJK-67890");
}

TEST_F(HostSessionTest, ManualRevocationRevokesAtTheServerAndIssuesANewInvite) {
    create();
    const auto old_token = current_invite_token();
    session().revoke_invite();
    ASSERT_EQ(rendezvous_.revocations().size(), 1U);
    EXPECT_EQ(rendezvous_.revocations().front().first.normalized(), "ABCDE12345");
    ASSERT_EQ(rendezvous_.registrations().size(), 2U);
    register_latest_invite("FGHJK67890");
    send_from(5, wire::JoinRequest{old_token, std::nullopt, "Vo"});
    EXPECT_EQ(denied_reason(5), ErrorCode::kInviteRevoked);
}

// --- 承認前の遮断、承認と拒否 ---

TEST_F(HostSessionTest, JoinRequestShowsInTheApprovalPanelWithoutAStripOrMedia) {
    create();
    request_join(7, "Tp 渡辺");
    const auto s = session().snapshot();
    ASSERT_EQ(s.pending.size(), 1U);
    EXPECT_EQ(s.pending.front().name, "Tp 渡辺");
    EXPECT_TRUE(s.channels.empty());
    // 承認前の接続のメディアは受け付けない（承認前の遮断）。
    EXPECT_TRUE(peers_.admitted().empty());
    EXPECT_EQ(mixer_.add_count(), 0);
    EXPECT_TRUE(std::holds_alternative<wire::JoinPending>(*last_sent(7)));
    const auto notice = ui_.last<cp::Notice>();
    ASSERT_TRUE(notice);
    EXPECT_EQ(notice->code, "join_request");
    EXPECT_EQ(notice->params.at("name"), "Tp 渡辺");
}

TEST_F(HostSessionTest, ApprovalCreatesAMutedStripAndAdmitsTheConnection) {
    create();
    request_join(7, "Tp 渡辺");
    ASSERT_TRUE(command("pending1", cp::ApproveParticipant{}));
    const auto s = session().snapshot();
    EXPECT_TRUE(s.pending.empty());
    ASSERT_EQ(s.channels.size(), 1U);
    const auto& c = s.channels.front();
    EXPECT_EQ(c.connection_state, ConnectionState::kConnecting);
    EXPECT_TRUE(c.channel_muted);
    EXPECT_TRUE(c.send_muted);
    EXPECT_TRUE(mixer_.channels().at(c.entity_id).muted);
    EXPECT_EQ(peers_.admitted().at(7), c.entity_id);
    EXPECT_TRUE(approved_token(7));

    session().on_peer_established(7);
    EXPECT_EQ(strip(c.entity_id).connection_state, ConnectionState::kConnected);
    EXPECT_EQ(strip(c.entity_id).signal_state, cp::SignalState::kPaused);
    EXPECT_EQ(session().snapshot().session.state, SessionState::kActive);
}

TEST_F(HostSessionTest, RejectionDeniesTheParticipantWithoutCreatingAStrip) {
    create();
    request_join(7, "Tp");
    ASSERT_TRUE(command("pending1", cp::RejectParticipant{}));
    EXPECT_TRUE(session().snapshot().pending.empty());
    EXPECT_TRUE(session().snapshot().channels.empty());
    EXPECT_EQ(denied_reason(7), ErrorCode::kRejected);
    EXPECT_TRUE(peers_.closed(7));
}

TEST_F(HostSessionTest, DropsAPendingRequestWhoseConnectionTimesOut) {
    create();
    request_join(7, "Tp");
    session().on_peer_timeout(7);
    EXPECT_TRUE(session().snapshot().pending.empty());
    EXPECT_TRUE(peers_.closed(7));
}

TEST_F(HostSessionTest, LimitsTheNumberOfPendingRequests) {
    create();
    for (PeerHandle peer = 1; peer <= 16; ++peer) {
        request_join(peer, "p");
    }
    request_join(17, "p");
    EXPECT_EQ(session().snapshot().pending.size(), 16U);
    EXPECT_EQ(denied_reason(17), ErrorCode::kRejected);
}

// --- 切断と同じストリップへの復帰（AT-A05 のドメイン部分） ---

TEST_F(HostSessionTest, DisconnectedParticipantReturnsToTheSameStripWithinTheGracePeriod) {
    create();
    const auto a = connect(1, "Vo");
    const auto b = connect(2, "Gt");
    const auto c = connect(3, "Ba");
    const auto token_b = approved_token(2);
    ASSERT_TRUE(token_b);
    ASSERT_TRUE(command(b, cp::SetChannelMute{false}));
    const auto adds_before = mixer_.add_count();

    session().on_peer_timeout(2);
    EXPECT_EQ(strip(b).connection_state, ConnectionState::kReconnecting);
    EXPECT_EQ(strip(a).connection_state, ConnectionState::kConnected);
    EXPECT_EQ(strip(c).connection_state, ConnectionState::kConnected);
    EXPECT_EQ(session().snapshot().session.state, SessionState::kActive);
    EXPECT_EQ(ui_.last<cp::Notice>()->code, "participant_reconnecting");

    clock_.advance(119s);
    session().tick();
    // 新しい接続（別のハンドル）から再接続トークンで戻る。承認は要らない。
    send_from(20, wire::ReconnectRequest{*token_b});
    EXPECT_EQ(approved_token(20), token_b);
    EXPECT_TRUE(peers_.closed(2));
    EXPECT_EQ(peers_.admitted().at(20), b);
    session().on_peer_established(20);

    const auto s = session().snapshot();
    EXPECT_EQ(s.channels.size(), 3U);  // ストリップは増えない
    EXPECT_EQ(mixer_.add_count(), adds_before);
    EXPECT_EQ(strip(b).connection_state, ConnectionState::kConnected);
    EXPECT_EQ(strip(b).participant_id, "p2");
    EXPECT_FALSE(strip(b).channel_muted);  // 復帰しても操作者の設定は変わらない
    EXPECT_TRUE(s.pending.empty());
}

TEST_F(HostSessionTest, GraceExpiryLeavesALostStripAndAllLostReturnsToReady) {
    create();
    const auto a = connect(1, "Vo");
    session().on_peer_timeout(1);
    clock_.advance(120s);
    session().tick();
    EXPECT_EQ(strip(a).connection_state, ConnectionState::kLost);
    EXPECT_TRUE(peers_.closed(1));
    EXPECT_EQ(session().snapshot().session.state, SessionState::kReady);
    EXPECT_EQ(ui_.last<cp::Notice>()->code, "participant_lost");
}

TEST_F(HostSessionTest, LostParticipantCanReturnToTheSameStripWithItsToken) {
    create();
    const auto a = connect(1, "Vo");
    const auto token = approved_token(1);
    session().on_peer_closed(1);
    EXPECT_EQ(strip(a).connection_state, ConnectionState::kLost);

    // 子機が招待で参加し直すときにトークンを添えれば、承認なしで同じストリップへ戻る。
    request_join(30, "Vo", token);
    EXPECT_EQ(approved_token(30), token);
    EXPECT_TRUE(session().snapshot().pending.empty());
    session().on_peer_established(30);
    EXPECT_EQ(session().snapshot().channels.size(), 1U);
    EXPECT_EQ(strip(a).connection_state, ConnectionState::kConnected);
    EXPECT_EQ(session().snapshot().session.state, SessionState::kActive);
}

TEST_F(HostSessionTest, ResumedConnectionReturnsToConnected) {
    create();
    const auto a = connect(1, "Vo");
    session().on_peer_timeout(1);
    session().on_peer_resumed(1);
    EXPECT_EQ(strip(a).connection_state, ConnectionState::kConnected);
}

TEST_F(HostSessionTest, PathFailureMarksTheStripFailedWithACause) {
    create();
    request_join(1, "Vo");
    ASSERT_TRUE(command("pending1", cp::ApproveParticipant{}));
    session().on_peer_path_failed(1);
    const auto c = session().snapshot().channels.front();
    EXPECT_EQ(c.connection_state, ConnectionState::kFailed);
    EXPECT_EQ(c.error_code, ErrorCode::kUnreachable);
}

TEST_F(HostSessionTest, RemovingAStripInvalidatesItsReconnectToken) {
    create();
    const auto a = connect(1, "Vo");
    const auto token = approved_token(1);
    // 接続中のストリップは削除できない。
    EXPECT_FALSE(command(a, cp::RemoveStrip{}));
    EXPECT_EQ(last_reject_, ErrorCode::kInvalidState);

    session().on_peer_closed(1);
    ASSERT_TRUE(command(a, cp::RemoveStrip{}));
    EXPECT_TRUE(session().snapshot().channels.empty());
    EXPECT_FALSE(mixer_.channels().contains(a));
    send_from(40, wire::ReconnectRequest{*token});
    EXPECT_EQ(denied_reason(40), ErrorCode::kReconnectTimeout);
}

// --- 招待と権限の検証（AT-A14 のドメイン部分） ---

TEST_F(HostSessionTest, DeniesJoinRequestsWithExpiredOrUnknownInvites) {
    create();
    const auto expired = current_invite_token();
    clock_.advance(30min);
    session().tick();
    register_latest_invite("FGHJK67890");
    send_from(1, wire::JoinRequest{expired, std::nullopt, "x"});
    EXPECT_EQ(denied_reason(1), ErrorCode::kInviteExpired);
    InviteToken unknown{};
    unknown.fill(0xEE);
    send_from(2, wire::JoinRequest{unknown, std::nullopt, "x"});
    EXPECT_EQ(denied_reason(2), ErrorCode::kInviteInvalid);
    EXPECT_TRUE(session().snapshot().pending.empty());
}

TEST_F(HostSessionTest, IgnoresMessagesAClientIsNotAllowedToSend) {
    create();
    const auto a = connect(1, "Vo");
    ASSERT_TRUE(command("master", cp::SetMasterMute{false}));
    const auto before = session().snapshot();
    // 改変した子機が Master の操作のような未知の種別や、親機から子機への種別を送っても捨てる。
    const std::vector<std::uint8_t> master_gain{0x10, 0x7F};
    session().on_peer_message(1, master_gain);
    session().on_peer_message(1,
                              wire::encode(wire::HostToClientMessage{wire::SessionEndedNotice{}}));
    EXPECT_EQ(session().snapshot(), before);
    EXPECT_FALSE(mixer_.master_muted());
    EXPECT_EQ(strip(a).connection_state, ConnectionState::kConnected);
}

TEST_F(HostSessionTest, ClientSendMuteReportCannotUnmuteTheChannel) {
    create();
    const auto a = connect(1, "Vo");
    const auto revision = strip(a).revision;
    send_from(1, wire::SendMuteReport{false, 1});
    EXPECT_FALSE(strip(a).send_muted);
    EXPECT_TRUE(strip(a).channel_muted);
    EXPECT_TRUE(mixer_.channels().at(a).muted);
    EXPECT_EQ(peers_.send_mutes().at(1).epoch, 1U);
    // 子機の送信ミュートは観測値なので revision を進めない。
    EXPECT_EQ(strip(a).revision, revision);
}

TEST_F(HostSessionTest, IgnoresReportsFromConnectionsWithoutAStrip) {
    create();
    request_join(1, "Vo");
    send_from(1, wire::SendMuteReport{false, 1});
    send_from(9, wire::SendMuteReport{false, 1});
    EXPECT_TRUE(peers_.send_mutes().empty());
}

// --- コマンドの適用と revision ---

TEST_F(HostSessionTest, ChannelMuteAdvancesTheRevisionButConnectionChangesDoNot) {
    create();
    request_join(1, "Vo");
    ASSERT_TRUE(command("pending1", cp::ApproveParticipant{}));
    const auto a = session().snapshot().channels.front().entity_id;
    const auto initial = strip(a).revision;
    session().on_peer_established(1);
    session().on_signal_state(1, cp::SignalState::kReceiving);
    EXPECT_EQ(strip(a).revision, initial);
    EXPECT_EQ(strip(a).signal_state, cp::SignalState::kReceiving);

    const auto applied = command(a, cp::SetChannelMute{false});
    ASSERT_TRUE(applied);
    EXPECT_EQ(*applied, initial + 1);
    EXPECT_FALSE(mixer_.channels().at(a).muted);
}

TEST_F(HostSessionTest, RejectsCommandsWithAStaleRevision) {
    create();
    const auto a = connect(1, "Vo");
    const auto stale = strip(a).revision;
    ASSERT_TRUE(command(a, cp::SetChannelMute{false}));
    EXPECT_FALSE(command(a, cp::SetChannelMute{true}, stale));
    EXPECT_EQ(last_reject_, ErrorCode::kRevisionConflict);
    EXPECT_FALSE(strip(a).channel_muted);
}

TEST_F(HostSessionTest, IgnoresADuplicatedRequestId) {
    create();
    const auto a = connect(1, "Vo");
    const auto revision = strip(a).revision;
    const cp::DiscreteCommand mute{"dup", a, revision, cp::SetChannelMute{false}};
    session().handle_ui_message(mute);
    const auto acks = ui_.all<cp::Ack>().size();
    session().handle_ui_message(mute);
    EXPECT_EQ(ui_.all<cp::Ack>().size(), acks);
    EXPECT_TRUE(ui_.all<cp::Reject>().empty());
}

TEST_F(HostSessionTest, RejectsUnknownEntitiesAndCommandsForTheWrongKindOfEntity) {
    create();
    const auto a = connect(1, "Vo");
    EXPECT_FALSE(command("ch99", cp::SetChannelMute{false}, 1));
    EXPECT_EQ(last_reject_, ErrorCode::kNotFound);
    EXPECT_FALSE(command(a, cp::SetMasterMute{false}));
    EXPECT_EQ(last_reject_, ErrorCode::kInvalidState);
    EXPECT_FALSE(command("master", cp::SetChannelMute{false}));
    EXPECT_EQ(last_reject_, ErrorCode::kInvalidState);
    // 子機のコマンドは親機では受け付けない。
    EXPECT_FALSE(command("session", cp::Join{"ABCDE-12345"}));
    EXPECT_EQ(last_reject_, ErrorCode::kInvalidState);
}

TEST_F(HostSessionTest, ReplaysTheFullStateToANewlyConnectedUi) {
    create();
    connect(1, "Vo");
    request_join(2, "Gt");
    session().handle_ui_message(cp::Hello{});
    const auto snapshot = ui_.last<cp::MixerSnapshotEvent>();
    ASSERT_TRUE(snapshot);
    EXPECT_EQ(snapshot->snapshot, session().snapshot());
    EXPECT_EQ(snapshot->snapshot.channels.size(), 1U);
    EXPECT_EQ(snapshot->snapshot.pending.size(), 1U);
}

// --- フェーダー ---

TEST_F(HostSessionTest, FaderClampsToPlusTenAndDropsOutOfOrderUpdates) {
    create();
    const auto a = connect(1, "Vo");
    const auto revision = strip(a).revision;
    session().handle_ui_message(cp::SetGain{a, 5, 12.0});
    EXPECT_EQ(strip(a).gain_db, cp::GainDb{10.0});
    EXPECT_EQ(mixer_.channels().at(a).gain_db, cp::GainDb{10.0});
    session().handle_ui_message(cp::SetGain{a, 4, -20.0});
    EXPECT_EQ(strip(a).gain_db, cp::GainDb{10.0});
    session().handle_ui_message(cp::SetGain{a, 6, std::nullopt});
    EXPECT_EQ(mixer_.channels().at(a).gain_db, std::nullopt);

    const auto acks = ui_.all<cp::GainAck>();
    ASSERT_EQ(acks.size(), 2U);
    EXPECT_EQ(acks.back().seq, 6U);
    EXPECT_EQ(acks.back().applied_revision, revision);  // 連続値では revision を進めない
}

TEST_F(HostSessionTest, MasterFaderIsSavedWithTheSettings) {
    create();
    session().handle_ui_message(cp::SetGain{"master", 1, -3.0});
    EXPECT_EQ(mixer_.master_gain_db(), cp::GainDb{-3.0});
    ASSERT_TRUE(command("session", cp::EndSession{}));
    EXPECT_EQ(settings_.host()->master_gain_db, cp::GainDb{-3.0});
}

// --- 出力デバイス ---

TEST_F(HostSessionTest, OpensOnlyTheSavedOutputDeviceAtStart) {
    outputs_.add("dev-1", "スピーカー");
    outputs_.add("dev-2", "USB Audio");
    settings_.host() = HostSettings{.output_device_id = "dev-2"};

    const auto s = session().snapshot();
    EXPECT_EQ(outputs_.opened(), "dev-2");
    EXPECT_EQ(s.session.output_device_state, cp::OutputDeviceState::kOpen);
    EXPECT_EQ(s.session.output_device_id, "dev-2");
    EXPECT_EQ(s.session.output_device_name, "USB Audio");
    EXPECT_EQ(s.session.output_devices.size(), 2U);
    // 出力デバイスを開いても、Master はミュートのまま始める。
    EXPECT_TRUE(mixer_.master_muted());
}

TEST_F(HostSessionTest, DoesNotFallBackWhenTheSavedOutputDeviceIsGone) {
    outputs_.add("dev-1", "スピーカー");
    settings_.host() = HostSettings{.output_device_id = "dev-gone"};

    const auto s = session().snapshot();
    EXPECT_EQ(outputs_.opened(), std::nullopt);
    EXPECT_EQ(s.session.output_device_state, cp::OutputDeviceState::kNone);
    EXPECT_EQ(s.session.output_device_id, std::nullopt);
    // 次の起動でもまた試せるよう、保存した ID は消さない。
    EXPECT_EQ(settings_.host()->output_device_id, "dev-gone");
}

TEST_F(HostSessionTest, SelectOutputOpensTheDeviceAndSavesIt) {
    outputs_.add("dev-1", "スピーカー");
    outputs_.add("dev-2", "USB Audio");

    ASSERT_TRUE(command("session", cp::SelectOutput{"dev-1"}));
    EXPECT_EQ(outputs_.opened(), "dev-1");
    ASSERT_TRUE(command("session", cp::SelectOutput{"dev-2"}));
    EXPECT_EQ(outputs_.opened(), "dev-2");
    EXPECT_FALSE(outputs_.opened_while_open());

    const auto s = session().snapshot();
    EXPECT_EQ(s.session.output_device_state, cp::OutputDeviceState::kOpen);
    EXPECT_EQ(s.session.output_device_name, "USB Audio");
    EXPECT_EQ(settings_.host()->output_device_id, "dev-2");
    EXPECT_EQ(ui_.last<cp::SessionUpdated>()->session.output_device_id, "dev-2");
    // 選んでも Master のミュートは変えない。
    EXPECT_TRUE(mixer_.master_muted());
}

TEST_F(HostSessionTest, SelectingTheOpenDeviceAgainKeepsItOpen) {
    outputs_.add("dev-1", "スピーカー");
    const auto opened = command("session", cp::SelectOutput{"dev-1"});
    ASSERT_TRUE(opened);
    const auto saves = settings_.save_count();

    EXPECT_EQ(command("session", cp::SelectOutput{"dev-1"}), opened);
    EXPECT_EQ(outputs_.opened(), "dev-1");
    EXPECT_EQ(settings_.save_count(), saves);
}

TEST_F(HostSessionTest, SelectOutputRejectsADeviceThatCannotBeOpened) {
    outputs_.add("dev-1", "スピーカー");
    ASSERT_TRUE(command("session", cp::SelectOutput{"dev-1"}));

    EXPECT_FALSE(command("session", cp::SelectOutput{"dev-missing"}));
    EXPECT_EQ(last_reject_, ErrorCode::kDeviceUnavailable);
    // 前のデバイスは閉じ、ほかのデバイスへは切り替えない。
    EXPECT_EQ(outputs_.opened(), std::nullopt);
    const auto s = session().snapshot();
    EXPECT_EQ(s.session.output_device_state, cp::OutputDeviceState::kNone);
    EXPECT_EQ(s.session.output_device_id, std::nullopt);
    EXPECT_EQ(ui_.last<cp::SessionUpdated>()->session.output_device_state,
              cp::OutputDeviceState::kNone);
}

TEST_F(HostSessionTest, SelectingNoOutputClosesTheDeviceAndForgetsIt) {
    outputs_.add("dev-1", "スピーカー");
    ASSERT_TRUE(command("session", cp::SelectOutput{"dev-1"}));

    ASSERT_TRUE(command("session", cp::SelectOutput{std::nullopt}));
    EXPECT_EQ(outputs_.opened(), std::nullopt);
    EXPECT_EQ(session().snapshot().session.output_device_state, cp::OutputDeviceState::kNone);
    EXPECT_EQ(settings_.host()->output_device_id, std::nullopt);
}

TEST_F(HostSessionTest, SelectOutputTargetsOnlyTheSessionEntity) {
    outputs_.add("dev-1", "スピーカー");
    EXPECT_FALSE(command("master", cp::SelectOutput{"dev-1"}));
    EXPECT_EQ(last_reject_, ErrorCode::kInvalidState);
    EXPECT_EQ(outputs_.opened(), std::nullopt);
}

TEST_F(HostSessionTest, HelloListsTheOutputDevicesAgain) {
    session();
    outputs_.add("dev-new", "挿したデバイス");
    session().handle_ui_message(cp::Hello{});
    const auto snapshot = ui_.last<cp::MixerSnapshotEvent>();
    ASSERT_TRUE(snapshot);
    const auto& devices = snapshot->snapshot.session.output_devices;
    ASSERT_EQ(devices.size(), 1U);
    EXPECT_EQ(devices[0].id, "dev-new");
}

// --- 目標レイテンシー ---

TEST_F(HostSessionTest, AppliesTheDefaultLatencyTargetAtStart) {
    session();
    EXPECT_EQ(playout_.target(), 20ms);
    EXPECT_EQ(session().snapshot().session.latency_target_ms, kDefaultLatencyTargetMs);
}

TEST_F(HostSessionTest, RestoresTheSavedLatencyTargetAndIgnoresAnOutOfRangeOne) {
    HostSettings saved;
    saved.latency_target_ms = 45.5;
    settings_.host() = saved;
    session();
    EXPECT_EQ(playout_.target(), 45500us);
    EXPECT_EQ(session().snapshot().session.latency_target_ms, 45.5);

    session_.reset();
    saved.latency_target_ms = 5000.0;
    settings_.host() = saved;
    session();
    EXPECT_EQ(playout_.target(), 20ms);
}

TEST_F(HostSessionTest, SettingTheLatencyTargetAdvancesTheRevisionAndIsSaved) {
    const auto before = revision_of("session");
    const auto applied = command("session", cp::SetLatencyTarget{60.0});
    ASSERT_TRUE(applied);
    EXPECT_GT(*applied, before);
    EXPECT_EQ(playout_.target(), 60ms);
    EXPECT_EQ(session().snapshot().session.latency_target_ms, 60.0);
    ASSERT_TRUE(settings_.host());
    EXPECT_EQ(settings_.host()->latency_target_ms, 60.0);
}

TEST_F(HostSessionTest, LatencyTargetBelongsToTheSessionEntity) {
    EXPECT_FALSE(command("master", cp::SetLatencyTarget{60.0}));
    EXPECT_EQ(last_reject_, ErrorCode::kInvalidState);
    EXPECT_EQ(playout_.target(), 20ms);
}

// --- 出力デバイスの抜去 ---

TEST_F(HostSessionTest, LosingTheOutputDeviceSafetyMutesTheMaster) {
    outputs_.add("dev-1", "スピーカー");
    outputs_.add("dev-2", "USB Audio");
    ASSERT_TRUE(command("session", cp::SelectOutput{"dev-1"}));
    ASSERT_TRUE(command("master", cp::SetMasterMute{false}));
    EXPECT_FALSE(mixer_.master_muted());
    const auto stale = revision_of("master");

    outputs_.remove("dev-1");
    session().on_output_device_lost();
    const auto s = session().snapshot();
    EXPECT_EQ(outputs_.opened(), std::nullopt);
    EXPECT_EQ(s.session.output_device_state, cp::OutputDeviceState::kLost);
    // 挿し直したデバイスを選び直しやすいよう、ID は残す。
    EXPECT_EQ(s.session.output_device_id, "dev-1");
    EXPECT_EQ(s.session.output_devices.size(), 1U);
    EXPECT_TRUE(s.master.master_muted);
    EXPECT_EQ(s.master.master_mute_reason, cp::MasterMuteReason::kSafety);
    EXPECT_TRUE(mixer_.master_muted());
    EXPECT_EQ(ui_.last<cp::Notice>()->code, "output_lost");

    // ミュートされたことを知らないまま押した解除は拒否する。
    EXPECT_FALSE(command("master", cp::SetMasterMute{false}, stale));
    EXPECT_EQ(last_reject_, ErrorCode::kRevisionConflict);
    EXPECT_TRUE(mixer_.master_muted());

    // 新しいデバイスを開いても、ミュートはオペレーターが解除するまで続く。
    ASSERT_TRUE(command("session", cp::SelectOutput{"dev-2"}));
    EXPECT_EQ(outputs_.opened(), "dev-2");
    EXPECT_TRUE(mixer_.master_muted());
    ASSERT_TRUE(command("master", cp::SetMasterMute{false}));
    EXPECT_FALSE(mixer_.master_muted());
    EXPECT_EQ(session().snapshot().master.master_mute_reason, std::nullopt);
}

TEST_F(HostSessionTest, EngineFaultSafetyMutesAndEntersError) {
    create();
    ASSERT_TRUE(command("master", cp::SetMasterMute{false}));
    session().on_engine_fault(ErrorCode::kEngineError);
    EXPECT_EQ(session().snapshot().session.state, SessionState::kError);
    EXPECT_TRUE(mixer_.master_muted());
}

// --- セッションの終了 ---

TEST_F(HostSessionTest, EndingTheSessionRevokesInvitesDisconnectsAndMutes) {
    create();
    const auto a = connect(1, "Vo");
    request_join(2, "Gt");
    ASSERT_TRUE(command("master", cp::SetMasterMute{false}));

    ASSERT_TRUE(command("session", cp::EndSession{}));
    const auto s = session().snapshot();
    EXPECT_EQ(s.session.state, SessionState::kIdle);
    EXPECT_EQ(s.session.invite_url, std::nullopt);
    EXPECT_TRUE(s.channels.empty());
    EXPECT_TRUE(s.pending.empty());
    EXPECT_TRUE(s.master.master_muted);
    EXPECT_TRUE(mixer_.master_muted());
    EXPECT_TRUE(mixer_.channels().empty());
    ASSERT_EQ(rendezvous_.revocations().size(), 1U);
    EXPECT_TRUE(std::holds_alternative<wire::SessionEndedNotice>(*last_sent(1)));
    EXPECT_EQ(denied_reason(2), ErrorCode::kSessionEnded);
    EXPECT_TRUE(peers_.closed(1));
    EXPECT_TRUE(peers_.closed(2));
    EXPECT_GE(settings_.save_count(), 1);
    ASSERT_FALSE(ui_.all<cp::ChannelRemoved>().empty());
    EXPECT_EQ(ui_.all<cp::ChannelRemoved>().back().entity_id, a);

    // 終了後は、古い再接続トークンや招待では入れない。
    request_join(3, "Vo");
    EXPECT_EQ(denied_reason(3), ErrorCode::kSessionEnded);
}

TEST_F(HostSessionTest, RevokesAnInviteThatFinishesRegisteringAfterTheSessionEnded) {
    ASSERT_TRUE(command("session", cp::CreateSession{}));
    const auto token = current_invite_token();
    register_latest_invite("ABCDE12345");
    clock_.advance(30min);
    session().tick();  // 発行し直しの要求が出る
    ASSERT_TRUE(command("session", cp::EndSession{}));
    const auto revocations = rendezvous_.revocations().size();
    // 終了後に届いた登録の結果は、すぐに失効させる。
    register_latest_invite("FGHJK67890");
    EXPECT_EQ(rendezvous_.revocations().size(), revocations + 1);
    EXPECT_NE(rendezvous_.registrations().back().invite_token, token);
}

// --- ルーティング ---

/// ch に返しのキューバスをプリフェーダーで送り、追加の出力から出す構成。
cp::RoutingConfig cue_routing(const std::string& strip_id) {
    cp::RoutingConfig config;
    config.buses.push_back(cp::RoutingBus{"bus-cue", "返し", cp::BusKind::kCue});
    config.outputs.push_back(cp::RoutingOutput{"out-mon", "モニター", 2});
    config.sends.push_back(cp::RoutingSend{"send-1", strip_id, "bus-cue", cp::SendTap::kPreFader});
    config.sends.push_back(
        cp::RoutingSend{"send-2", "bus-cue", "out-mon", cp::SendTap::kPostFader});
    config.input_delays.push_back(cp::InputDelay{strip_id, 5.0});
    return config;
}

/// Master から遅延ノードを経て Master へ戻るループの構成。
cp::RoutingConfig echo_routing() {
    cp::RoutingConfig config;
    config.buses.push_back(cp::RoutingBus{"bus-fx", "FX", cp::BusKind::kUser});
    config.delays.push_back(cp::RoutingDelay{"delay-echo", 250.0});
    config.sends.push_back(
        cp::RoutingSend{"send-1", "master", "delay-echo", cp::SendTap::kPostFader});
    config.sends.push_back(
        cp::RoutingSend{"send-2", "delay-echo", "bus-fx", cp::SendTap::kPostFader});
    config.sends.push_back(cp::RoutingSend{"send-3", "bus-fx", "master", cp::SendTap::kPostFader});
    return config;
}

TEST_F(HostSessionTest, StagingDoesNotTouchTheMixerUntilCommitted) {
    create();
    const auto a = connect(1, "Vo");
    const auto staged = command("routing", cp::StageRouting{cue_routing(a)});
    ASSERT_TRUE(staged);
    EXPECT_EQ(*staged, revision_of("routing"));
    EXPECT_EQ(mixer_.apply_routing_count(), 0);
    const auto routing = session().snapshot().routing;
    ASSERT_TRUE(routing.staged);
    EXPECT_FALSE(routing.staged->check.issue);
    EXPECT_EQ(routing.applied, cp::RoutingConfig{});

    const auto committed = command("routing", cp::CommitRouting{false});
    ASSERT_TRUE(committed);
    EXPECT_GT(*committed, *staged);
    EXPECT_EQ(mixer_.routing(), cue_routing(a));
    EXPECT_FALSE(mixer_.feedback_approved());
    EXPECT_EQ(session().snapshot().routing.applied, cue_routing(a));
    EXPECT_FALSE(session().snapshot().routing.staged);
}

TEST_F(HostSessionTest, NewBusesAndSendsStartMuted) {
    create();
    const auto a = connect(1, "Vo");
    ASSERT_TRUE(command("routing", cp::StageRouting{cue_routing(a)}));
    ASSERT_TRUE(command("routing", cp::CommitRouting{false}));
    for (const auto& level : session().snapshot().routing.levels) {
        EXPECT_TRUE(level.muted) << level.entity_id;
    }
    EXPECT_TRUE(mixer_.routes().at("send-1").muted);
    EXPECT_TRUE(mixer_.routes().at("bus-cue").muted);
}

TEST_F(HostSessionTest, LoopsThroughADelayNodeNeedTheOperatorsApproval) {
    ASSERT_TRUE(command("routing", cp::StageRouting{echo_routing()}));
    const auto staged = session().snapshot().routing.staged;
    ASSERT_TRUE(staged);
    EXPECT_TRUE(staged->check.needs_approval);
    EXPECT_FALSE(command("routing", cp::CommitRouting{false}));
    EXPECT_EQ(last_reject_, ErrorCode::kInvalidState);
    EXPECT_EQ(mixer_.apply_routing_count(), 0);

    ASSERT_TRUE(command("routing", cp::CommitRouting{true}));
    EXPECT_TRUE(mixer_.feedback_approved());
    EXPECT_TRUE(session().snapshot().routing.feedback_approved);
}

TEST_F(HostSessionTest, RejectsCommittingAnUndelayedLoopEvenWithApproval) {
    // 遅延ノードを通らずに Master へ戻る。
    auto config = echo_routing();
    config.sends[0].to = "bus-fx";
    config.sends.erase(config.sends.begin() + 1);
    ASSERT_TRUE(command("routing", cp::StageRouting{config}));
    EXPECT_EQ(session().snapshot().routing.staged->check.issue, cp::RoutingIssue::kUndelayedLoop);
    EXPECT_FALSE(command("routing", cp::CommitRouting{true}));
    EXPECT_EQ(last_reject_, ErrorCode::kInvalidState);
    EXPECT_EQ(mixer_.apply_routing_count(), 0);
}

TEST_F(HostSessionTest, KeepsTheCurrentRoutingWhenTheMixerRefusesIt) {
    mixer_.reject_routing();
    ASSERT_TRUE(command("routing", cp::StageRouting{echo_routing()}));
    EXPECT_FALSE(command("routing", cp::CommitRouting{true}));
    EXPECT_EQ(last_reject_, ErrorCode::kEngineError);
    EXPECT_EQ(session().snapshot().routing.applied, cp::RoutingConfig{});
    EXPECT_TRUE(session().snapshot().routing.staged);
}

TEST_F(HostSessionTest, DiscardingTheStagedRoutingKeepsTheAppliedOne) {
    EXPECT_FALSE(command("routing", cp::DiscardRouting{}));
    EXPECT_EQ(last_reject_, ErrorCode::kInvalidState);
    ASSERT_TRUE(command("routing", cp::StageRouting{echo_routing()}));
    ASSERT_TRUE(command("routing", cp::DiscardRouting{}));
    EXPECT_FALSE(session().snapshot().routing.staged);
    EXPECT_EQ(mixer_.apply_routing_count(), 0);
}

TEST_F(HostSessionTest, RoutingCommandsCheckTheRevisionAndTheEntity) {
    const auto before = revision_of("routing");
    EXPECT_FALSE(command("routing", cp::StageRouting{echo_routing()}, before + 1));
    EXPECT_EQ(last_reject_, ErrorCode::kRevisionConflict);
    EXPECT_FALSE(command("master", cp::StageRouting{echo_routing()}));
    EXPECT_EQ(last_reject_, ErrorCode::kInvalidState);
    EXPECT_FALSE(command("routing", cp::SetRouteMute{false}));
    EXPECT_EQ(last_reject_, ErrorCode::kInvalidState);
    EXPECT_FALSE(command("bus-fx", cp::SetRouteMute{false}));
    EXPECT_EQ(last_reject_, ErrorCode::kNotFound);
}

TEST_F(HostSessionTest, RouteMuteAdvancesItsRevisionAndFadersDoNot) {
    ASSERT_TRUE(command("routing", cp::StageRouting{echo_routing()}));
    ASSERT_TRUE(command("routing", cp::CommitRouting{true}));
    const auto routing_revision = revision_of("routing");
    const auto before = revision_of("send-1");
    const auto unmuted = command("send-1", cp::SetRouteMute{false});
    ASSERT_TRUE(unmuted);
    EXPECT_EQ(*unmuted, before + 1);
    EXPECT_FALSE(mixer_.routes().at("send-1").muted);
    // バスとセンドの操作では、ルーティング全体の revision は進まない。
    EXPECT_EQ(revision_of("routing"), routing_revision);

    session().handle_ui_message(cp::SetGain{"send-1", 5, 15.0});
    EXPECT_EQ(mixer_.routes().at("send-1").gain_db, 10.0);
    session().handle_ui_message(cp::SetGain{"send-1", 4, -20.0});
    EXPECT_EQ(mixer_.routes().at("send-1").gain_db, 10.0);
    const auto* ack = std::get_if<cp::GainAck>(&ui_.messages().back());
    ASSERT_NE(ack, nullptr);
    EXPECT_EQ(ack->seq, 5U);
    EXPECT_EQ(ack->applied_revision, *unmuted);
}

TEST_F(HostSessionTest, RemovingAStripDropsItsSendsFromTheRouting) {
    create();
    const auto a = connect(1, "Vo");
    ASSERT_TRUE(command("routing", cp::StageRouting{cue_routing(a)}));
    ASSERT_TRUE(command("routing", cp::CommitRouting{false}));
    const auto before = revision_of("routing");

    session().on_peer_closed(1);
    ASSERT_TRUE(command(a, cp::RemoveStrip{}));
    const auto routing = session().snapshot().routing;
    EXPECT_GT(routing.revision, before);
    ASSERT_EQ(routing.applied.sends.size(), 1U);
    EXPECT_EQ(routing.applied.sends[0].entity_id, "send-2");
    EXPECT_TRUE(routing.applied.input_delays.empty());
    EXPECT_EQ(mixer_.routing(), routing.applied);
}

TEST_F(HostSessionTest, BusesAndOutputsOutliveTheSession) {
    create();
    const auto a = connect(1, "Vo");
    ASSERT_TRUE(command("routing", cp::StageRouting{cue_routing(a)}));
    ASSERT_TRUE(command("routing", cp::CommitRouting{false}));
    ASSERT_TRUE(command("session", cp::EndSession{}));
    const auto routing = session().snapshot().routing;
    EXPECT_EQ(routing.applied.buses.size(), 1U);
    EXPECT_EQ(routing.applied.outputs.size(), 1U);
    ASSERT_EQ(routing.applied.sends.size(), 1U);
    EXPECT_EQ(routing.applied.sends[0].entity_id, "send-2");
}

// --- ルーティングの保存と復元 ---

TEST_F(HostSessionTest, SavesTheRoutingWithoutStripReferencesWhenApplied) {
    create();
    const auto a = connect(1, "Vo");
    ASSERT_TRUE(command("routing", cp::StageRouting{cue_routing(a)}));
    ASSERT_TRUE(command("routing", cp::CommitRouting{false}));
    session().handle_ui_message(cp::SetGain{"bus-cue", 1, -3.0});
    ASSERT_TRUE(command("session", cp::EndSession{}));

    const auto& saved = *settings_.host();
    EXPECT_EQ(saved.routing.buses, cue_routing(a).buses);
    EXPECT_EQ(saved.routing.outputs, cue_routing(a).outputs);
    ASSERT_EQ(saved.routing.sends.size(), 1U);
    EXPECT_EQ(saved.routing.sends[0].entity_id, "send-2");
    EXPECT_TRUE(saved.routing.input_delays.empty());
    const std::vector<SavedRouteGain> gains{{"bus-cue", -3.0}, {"send-2", 0.0}};
    EXPECT_EQ(saved.route_gains, gains);
}

TEST_F(HostSessionTest, RestoresTheSavedRoutingMutedWithItsFaders) {
    auto config = echo_routing();
    config.sends.pop_back();  // ループを切り、承認の要らない構成にする
    HostSettings saved;
    saved.routing = config;
    saved.route_gains = {{"bus-fx", -6.0}, {"send-1", 20.0}, {"bus-gone", 3.0}};
    settings_.host() = saved;

    const auto routing = session().snapshot().routing;
    EXPECT_EQ(routing.applied, config);
    EXPECT_FALSE(routing.staged);
    EXPECT_EQ(mixer_.routing(), config);
    for (const auto& level : routing.levels) {
        EXPECT_TRUE(level.muted) << level.entity_id;
        EXPECT_TRUE(mixer_.routes().at(level.entity_id).muted) << level.entity_id;
    }
    EXPECT_EQ(session().snapshot().routing.levels[0].gain_db, cp::GainDb{-6.0});
    // フェーダーの範囲を超える値は、操作と同じ +10 dB に丸める。
    EXPECT_EQ(mixer_.routes().at("send-1").gain_db, cp::GainDb{10.0});
}

TEST_F(HostSessionTest, StagesASavedLoopUntilTheOperatorApprovesIt) {
    HostSettings saved;
    saved.routing = echo_routing();
    settings_.host() = saved;

    const auto routing = session().snapshot().routing;
    EXPECT_EQ(routing.applied, cp::RoutingConfig{});
    ASSERT_TRUE(routing.staged);
    EXPECT_TRUE(routing.staged->check.needs_approval);
    EXPECT_EQ(mixer_.apply_routing_count(), 0);
    ASSERT_TRUE(command("routing", cp::CommitRouting{true}));
    EXPECT_EQ(mixer_.routing(), echo_routing());
}

TEST_F(HostSessionTest, DoesNotRestoreARoutingWithAnIssue) {
    // Master とバスの間の、遅延のないループ。
    cp::RoutingConfig config;
    config.buses.push_back(cp::RoutingBus{"bus-fx", "FX", cp::BusKind::kUser});
    config.sends.push_back(cp::RoutingSend{"send-1", "master", "bus-fx", cp::SendTap::kPostFader});
    config.sends.push_back(cp::RoutingSend{"send-2", "bus-fx", "master", cp::SendTap::kPostFader});
    HostSettings saved;
    saved.routing = config;
    settings_.host() = saved;

    const auto routing = session().snapshot().routing;
    EXPECT_EQ(routing.applied, cp::RoutingConfig{});
    EXPECT_FALSE(routing.staged);
    EXPECT_EQ(mixer_.apply_routing_count(), 0);
}

TEST_F(HostSessionTest, DoesNotRestoreARoutingTheMixerRefuses) {
    mixer_.reject_routing();
    auto config = echo_routing();
    config.sends.pop_back();
    HostSettings saved;
    saved.routing = config;
    settings_.host() = saved;

    const auto routing = session().snapshot().routing;
    EXPECT_EQ(routing.applied, cp::RoutingConfig{});
    EXPECT_FALSE(routing.staged);
}

// --- プラグインの挿入 ---

std::vector<PluginDescription> insert_plugins() {
    PluginDescription eq;
    eq.name = "EQ";
    eq.identifier = "VST3-EQ-1";
    eq.input_channels = 2;
    eq.output_channels = 2;
    PluginDescription verb = eq;
    verb.name = "Verb";
    verb.identifier = "VST3-Verb-2";
    return {eq, verb};
}

std::shared_ptr<audio_core::PluginProcessor> fake_processor() {
    return std::make_shared<audio_core::FakePluginProcessor>();
}

/// ストリップに EQ を、FX バスにリバーブを挿す構成。
cp::RoutingConfig insert_routing(const std::string& strip_id) {
    cp::RoutingConfig config;
    config.buses.push_back(cp::RoutingBus{"bus-fx", "FX", cp::BusKind::kUser});
    config.sends.push_back(cp::RoutingSend{"send-1", strip_id, "bus-fx", cp::SendTap::kPostFader});
    config.inserts.push_back(cp::RoutingInsert{"fx-eq", strip_id, "VST3-EQ-1"});
    config.inserts.push_back(cp::RoutingInsert{"fx-verb", "bus-fx", "VST3-Verb-2"});
    return config;
}

TEST_F(HostSessionTest, InsertsNeedAPluginFromTheList) {
    create();
    const auto a = connect(1, "Vo");
    ASSERT_TRUE(command("routing", cp::StageRouting{insert_routing(a)}));
    EXPECT_EQ(session().snapshot().routing.staged->check.issue, cp::RoutingIssue::kUnknownPlugin);
    EXPECT_FALSE(command("routing", cp::CommitRouting{false}));

    // 一覧が届くと、ステージング中の構成を検証し直して知らせる。
    const auto revision = revision_of("routing");
    ui_.clear();
    session().set_available_plugins(insert_plugins());
    const auto routing = session().snapshot().routing;
    EXPECT_FALSE(routing.staged->check.issue);
    EXPECT_EQ(routing.revision, revision);
    EXPECT_TRUE(ui_.last<cp::RoutingUpdated>());
}

TEST_F(HostSessionTest, CommitAppliesInsertsSilentlyAndConnectsThemAsTheyLoad) {
    session().set_available_plugins(insert_plugins());
    create();
    const auto a = connect(1, "Vo");
    ASSERT_TRUE(command("routing", cp::StageRouting{insert_routing(a)}));
    ASSERT_TRUE(command("routing", cp::CommitRouting{false}));

    // 読み込みの前に適用し、挿入の経路は切ったまま（ミキサーに挿入を渡さない）。
    EXPECT_EQ(mixer_.routing(), insert_routing(a));
    EXPECT_TRUE(mixer_.plugins().empty());
    ASSERT_EQ(plugins_.loads().size(), 2U);
    EXPECT_EQ(plugins_.loads()[0].insert_id, "fx-eq");
    EXPECT_EQ(plugins_.loads()[1].plugin_id, "VST3-Verb-2");

    const auto revision = revision_of("routing");
    const auto count = mixer_.apply_routing_count();
    const auto eq = fake_processor();
    session().on_plugin_loaded("fx-eq", plugins_.loads()[0].ticket, eq);
    EXPECT_EQ(mixer_.apply_routing_count(), count + 1);
    ASSERT_EQ(mixer_.plugins().size(), 1U);
    EXPECT_EQ(mixer_.plugins()[0].entity_id, "fx-eq");
    EXPECT_EQ(mixer_.plugins()[0].processor, eq);
    // 読み込みは観測値の変化なので revision は進めない。
    EXPECT_EQ(revision_of("routing"), revision);

    // 読み込めなかった挿入はつながず、グラフも作り直さない。
    session().on_plugin_loaded("fx-verb", plugins_.loads()[1].ticket,
                               common::make_unexpected(PluginLoadError::kInstantiateFailed));
    EXPECT_EQ(mixer_.apply_routing_count(), count + 1);
    EXPECT_EQ(mixer_.plugins().size(), 1U);
}

TEST_F(HostSessionTest, RemovedInsertsAreReleasedAfterTheGraphNoLongerUsesThem) {
    session().set_available_plugins(insert_plugins());
    create();
    const auto a = connect(1, "Vo");
    ASSERT_TRUE(command("routing", cp::StageRouting{insert_routing(a)}));
    ASSERT_TRUE(command("routing", cp::CommitRouting{false}));
    session().on_plugin_loaded("fx-eq", plugins_.loads()[0].ticket, fake_processor());

    auto without_eq = insert_routing(a);
    without_eq.inserts.erase(without_eq.inserts.begin());
    ASSERT_TRUE(command("routing", cp::StageRouting{without_eq}));
    ASSERT_TRUE(command("routing", cp::CommitRouting{false}));
    EXPECT_TRUE(mixer_.plugins().empty());
    EXPECT_EQ(plugins_.releases(), std::vector<std::string>{"fx-eq"});
    // 外した挿入の遅れて届いた結果は使わない。
    session().on_plugin_loaded("fx-eq", plugins_.loads()[0].ticket, fake_processor());
    EXPECT_TRUE(mixer_.plugins().empty());
}

TEST_F(HostSessionTest, RemovingAStripReleasesItsInserts) {
    session().set_available_plugins(insert_plugins());
    create();
    const auto a = connect(1, "Vo");
    ASSERT_TRUE(command("routing", cp::StageRouting{insert_routing(a)}));
    ASSERT_TRUE(command("routing", cp::CommitRouting{false}));

    session().on_peer_closed(1);
    ASSERT_TRUE(command(a, cp::RemoveStrip{}));
    const auto& applied = session().snapshot().routing.applied;
    ASSERT_EQ(applied.inserts.size(), 1U);
    EXPECT_EQ(applied.inserts[0].entity_id, "fx-verb");
    EXPECT_EQ(plugins_.releases(), std::vector<std::string>{"fx-eq"});
}

TEST_F(HostSessionTest, ProcessingFaultsAreLoggedOnce) {
    session().set_available_plugins(insert_plugins());
    create();
    const auto a = connect(1, "Vo");
    ASSERT_TRUE(command("routing", cp::StageRouting{insert_routing(a)}));
    ASSERT_TRUE(command("routing", cp::CommitRouting{false}));
    session().on_plugin_loaded("fx-eq", plugins_.loads()[0].ticket, fake_processor());
    const auto warnings = [this] {
        const auto entries = logger_.entries();
        return std::count_if(entries.begin(), entries.end(), [](const auto& entry) {
            return entry.level == common::LogLevel::kWarning;
        });
    };
    const auto before = warnings();

    mixer_.plugins()[0].fault->raise();
    session().tick();
    session().tick();
    EXPECT_EQ(warnings(), before + 1);
}

TEST_F(HostSessionTest, SavesAndRestoresInsertsOnMasterAndBusesOnly) {
    session().set_available_plugins(insert_plugins());
    create();
    const auto a = connect(1, "Vo");
    ASSERT_TRUE(command("routing", cp::StageRouting{insert_routing(a)}));
    ASSERT_TRUE(command("routing", cp::CommitRouting{false}));
    ASSERT_TRUE(command("session", cp::EndSession{}));
    const auto& saved = *settings_.host();
    ASSERT_EQ(saved.routing.inserts.size(), 1U);
    EXPECT_EQ(saved.routing.inserts[0].entity_id, "fx-verb");
}

TEST_F(HostSessionTest, RestoresSavedInsertsAndDropsThoseOfMissingPlugins) {
    cp::RoutingConfig config;
    config.buses.push_back(cp::RoutingBus{"bus-fx", "FX", cp::BusKind::kUser});
    config.inserts.push_back(cp::RoutingInsert{"fx-verb", "bus-fx", "VST3-Verb-2"});
    config.inserts.push_back(cp::RoutingInsert{"fx-gone", "master", "VST3-Gone-3"});
    HostSettings saved;
    saved.routing = config;
    settings_.host() = saved;
    session_.emplace(
        config_,
        HostSession::Ports{ui_, mixer_, outputs_, peers_, rendezvous_, settings_, random_, clock_,
                           logger_, playout_, external_output_, secrets_, plugins_, plugin_scan_});
    session_->set_available_plugins(insert_plugins());
    session_->start();

    const auto& applied = session().snapshot().routing.applied;
    ASSERT_EQ(applied.inserts.size(), 1U);
    EXPECT_EQ(applied.inserts[0].entity_id, "fx-verb");
    ASSERT_EQ(plugins_.loads().size(), 1U);
    EXPECT_EQ(plugins_.loads()[0].insert_id, "fx-verb");
}

// --- 子機の複数の入力（ステレオ、複数入力、アプリ音声） ---

constexpr InputStream kMic{0, cp::InputSourceKind::kMicrophone, 1};
constexpr InputStream kAppAudio{1, cp::InputSourceKind::kApplicationAudio, 2};
constexpr InputStream kSecondMic{2, cp::InputSourceKind::kMicrophone, 1};

TEST_F(HostSessionTest, ApprovalCreatesAMutedStripForEveryDeclaredInput) {
    create();
    request_join(7, "Vo");
    send_from(7, wire::StreamsReport{{kMic, kAppAudio}});
    ASSERT_TRUE(command("pending1", cp::ApproveParticipant{}));

    const auto channels = session().snapshot().channels;
    ASSERT_EQ(channels.size(), 2U);
    EXPECT_EQ(channels[0].participant_id, channels[1].participant_id);
    EXPECT_EQ(channels[0].name, "Vo");
    EXPECT_EQ(channels[1].name, "Vo");
    EXPECT_EQ(channels[0].source_kind, cp::InputSourceKind::kMicrophone);
    EXPECT_EQ(channels[0].channel_count, 1);
    EXPECT_EQ(channels[1].source_kind, cp::InputSourceKind::kApplicationAudio);
    EXPECT_EQ(channels[1].channel_count, 2);
    for (const auto& c : channels) {
        EXPECT_TRUE(c.channel_muted);  // 初期ミュート
        EXPECT_TRUE(mixer_.channels().at(c.entity_id).muted);
    }
    const auto& routes = peers_.routes().at(7);
    ASSERT_EQ(routes.size(), 2U);
    EXPECT_EQ(routes[0], (StreamRoute{0, 1, channels[0].entity_id}));
    EXPECT_EQ(routes[1], (StreamRoute{1, 2, channels[1].entity_id}));
}

TEST_F(HostSessionTest, ClientWithoutStreamsReportGetsOneMonoMicrophoneStrip) {
    create();
    const auto a = connect(7, "Vo");
    EXPECT_EQ(session().snapshot().channels.size(), 1U);
    EXPECT_EQ(peers_.routes().at(7), (std::vector<StreamRoute>{{0, 1, a}}));
}

TEST_F(HostSessionTest, ConnectionStateAndSendMuteApplyToEveryStripOfTheClient) {
    create();
    request_join(7, "Vo");
    send_from(7, wire::StreamsReport{{kMic, kAppAudio}});
    ASSERT_TRUE(command("pending1", cp::ApproveParticipant{}));
    session().on_peer_established(7);
    const auto channels = session().snapshot().channels;

    send_from(7, wire::SendMuteReport{false, 1});
    for (const auto& c : channels) {
        EXPECT_FALSE(strip(c.entity_id).send_muted);
    }
    session().on_peer_timeout(7);
    for (const auto& c : channels) {
        EXPECT_EQ(strip(c.entity_id).connection_state, ConnectionState::kReconnecting);
    }
    clock_.advance(121s);
    session().tick();
    for (const auto& c : channels) {
        EXPECT_EQ(strip(c.entity_id).connection_state, ConnectionState::kLost);
    }
    // 同じ子機のストリップがそろって LOST になっても、通知は 1 回だけ。
    const auto lost =
        std::count_if(ui_.messages().begin(), ui_.messages().end(), [](const auto& m) {
            const auto* n = std::get_if<cp::Notice>(&m);
            return n != nullptr && n->code == "participant_lost";
        });
    EXPECT_EQ(lost, 1);
}

TEST_F(HostSessionTest, AddedInputGetsANewMutedStripAndKeepsTheExistingOne) {
    create();
    const auto a = connect(7, "Vo");
    ASSERT_TRUE(command(a, cp::SetChannelMute{false}));

    send_from(7, wire::StreamsReport{{kMic, kAppAudio}});
    const auto channels = session().snapshot().channels;
    ASSERT_EQ(channels.size(), 2U);
    EXPECT_EQ(channels[0].entity_id, a);
    EXPECT_FALSE(channels[0].channel_muted);  // 変わらない入力の操作者の設定は保つ
    EXPECT_TRUE(channels[1].channel_muted);
    EXPECT_EQ(channels[1].connection_state, ConnectionState::kConnected);
    EXPECT_EQ(channels[1].participant_id, channels[0].participant_id);
    EXPECT_EQ(peers_.routes().at(7).size(), 2U);
}

TEST_F(HostSessionTest, InputWhoseChannelCountChangesGetsANewStrip) {
    create();
    const auto a = connect(7, "Vo");
    ASSERT_TRUE(command(a, cp::SetChannelMute{false}));

    send_from(7, wire::StreamsReport{{InputStream{0, cp::InputSourceKind::kMicrophone, 2}}});
    const auto channels = session().snapshot().channels;
    ASSERT_EQ(channels.size(), 1U);
    EXPECT_NE(channels[0].entity_id, a);
    EXPECT_EQ(channels[0].channel_count, 2);
    EXPECT_TRUE(channels[0].channel_muted);
    EXPECT_FALSE(mixer_.channels().contains(a));
    EXPECT_TRUE(ui_.last<cp::ChannelRemoved>());
}

TEST_F(HostSessionTest, NumbersMicrophonesInStreamOrderAndRenumbersWhenOneIsRemoved) {
    create();
    request_join(7, "Vo");
    send_from(7, wire::StreamsReport{{kMic, kAppAudio, kSecondMic}});
    ASSERT_TRUE(command("pending1", cp::ApproveParticipant{}));
    auto channels = session().snapshot().channels;
    ASSERT_EQ(channels.size(), 3U);
    EXPECT_EQ(channels[0].source_number, 1);
    EXPECT_EQ(channels[1].source_number, 1);  // アプリ音声は別に数える
    EXPECT_EQ(channels[2].source_number, 2);

    send_from(7, wire::StreamsReport{{kAppAudio, kSecondMic}});
    channels = session().snapshot().channels;
    ASSERT_EQ(channels.size(), 2U);
    EXPECT_EQ(channels[1].source_kind, cp::InputSourceKind::kMicrophone);
    EXPECT_EQ(channels[1].source_number, 1);
}

TEST_F(HostSessionTest, RemovingAStripRemovesEveryStripOfTheDisconnectedClient) {
    create();
    request_join(7, "Vo");
    send_from(7, wire::StreamsReport{{kMic, kAppAudio}});
    ASSERT_TRUE(command("pending1", cp::ApproveParticipant{}));
    session().on_peer_established(7);
    send_from(7, wire::LeaveNotice{});
    const auto second = session().snapshot().channels[1].entity_id;

    ASSERT_TRUE(command(second, cp::RemoveStrip{}));
    EXPECT_TRUE(session().snapshot().channels.empty());
    EXPECT_TRUE(mixer_.channels().empty());
}

TEST_F(HostSessionTest, ReturnMuteAppliesToEveryStripOfTheClient) {
    // 返送は子機 1 台に 1 本なので、どの入力のストリップから操作しても子機全体に効く。
    create();
    request_join(7, "Vo");
    send_from(7, wire::StreamsReport{{kMic, kAppAudio}});
    ASSERT_TRUE(command("pending1", cp::ApproveParticipant{}));
    const auto channels = session().snapshot().channels;
    ASSERT_EQ(channels.size(), 2U);

    ASSERT_TRUE(command(channels[1].entity_id, cp::SetReturnMute{false}));
    for (const auto& c : session().snapshot().channels) {
        EXPECT_FALSE(c.return_muted) << c.entity_id;
        EXPECT_GT(c.revision, 1U) << c.entity_id;
    }
    EXPECT_FALSE(peers_.return_mutes().at(7).send_muted);
    EXPECT_EQ(peers_.return_mutes().at(7).epoch, 1U);

    // 後から足した入力のストリップも、子機の返送の今の状態を持つ。
    send_from(7, wire::StreamsReport{{kMic, kAppAudio, kSecondMic}});
    const auto after = session().snapshot().channels;
    ASSERT_EQ(after.size(), 3U);
    EXPECT_FALSE(after[2].return_muted);
    ASSERT_TRUE(command(after[2].entity_id, cp::SetReturnMute{true}));
    ASSERT_TRUE(command(after[0].entity_id, cp::SetReturnMute{false}));
    EXPECT_EQ(peers_.return_mutes().at(7).epoch, 2U);
}

TEST_F(HostSessionTest, RejectsApprovalWhenTheInputsDoNotFitInTheStripLimit) {
    create();
    for (PeerHandle peer = 1; peer < kMaxChannelStrips; ++peer) {
        static_cast<void>(connect(peer, "p"));
    }
    request_join(100, "Vo");
    send_from(100, wire::StreamsReport{{kMic, kAppAudio}});
    const auto pending = session().snapshot().pending.back().entity_id;
    EXPECT_FALSE(command(pending, cp::ApproveParticipant{}));
    EXPECT_EQ(last_reject_, ErrorCode::kInvalidState);
}

// --- 返送モニター（Minus-Me） ---

// 接続 peer に送った ReturnMuteNotice のうち最後のもの。
std::optional<wire::ReturnMuteNotice> last_return_notice(const FakePeerLink& peers,
                                                         PeerHandle peer) {
    const auto sent = peers.sent_to(peer);
    for (auto it = sent.rbegin(); it != sent.rend(); ++it) {
        auto decoded = wire::decode_host_to_client(*it);
        if (decoded) {
            if (const auto* notice = std::get_if<wire::ReturnMuteNotice>(&decoded.value())) {
                return *notice;
            }
        }
    }
    return std::nullopt;
}

TEST_F(HostSessionTest, ApprovedStripStartsWithTheReturnMutedAndTellsTheConnection) {
    create();
    const auto a = connect(1, "Vo");
    EXPECT_TRUE(strip(a).return_muted);
    // 返送の送信側は止めた状態で始まり、子機にもミュートを知らせる。
    ASSERT_TRUE(peers_.return_mutes().contains(1));
    EXPECT_TRUE(peers_.return_mutes().at(1).send_muted);
    const auto notice = last_return_notice(peers_, 1);
    ASSERT_TRUE(notice);
    EXPECT_TRUE(notice->muted);
}

TEST_F(HostSessionTest, UnmutingTheReturnStartsSendingWithANewEpoch) {
    create();
    const auto a = connect(1, "Vo");
    const auto before = strip(a).revision;
    const auto applied = command(a, cp::SetReturnMute{false});
    ASSERT_TRUE(applied);
    EXPECT_GT(*applied, before);
    EXPECT_FALSE(strip(a).return_muted);
    EXPECT_FALSE(peers_.return_mutes().at(1).send_muted);
    EXPECT_EQ(peers_.return_mutes().at(1).epoch, 1U);
    EXPECT_EQ(last_return_notice(peers_, 1), (wire::ReturnMuteNotice{false, 1}));
}

TEST_F(HostSessionTest, MutingTheReturnStopsSendingAndKeepsTheEpoch) {
    create();
    const auto a = connect(1, "Vo");
    ASSERT_TRUE(command(a, cp::SetReturnMute{false}));
    ASSERT_TRUE(command(a, cp::SetReturnMute{true}));
    EXPECT_TRUE(strip(a).return_muted);
    // ミュートしたら送信を止める（方向別のデータフロー規則）。同じ世代のうちに止まる。
    EXPECT_TRUE(peers_.return_mutes().at(1).send_muted);
    EXPECT_EQ(peers_.return_mutes().at(1).epoch, 1U);
    EXPECT_EQ(last_return_notice(peers_, 1), (wire::ReturnMuteNotice{true, 1}));

    ASSERT_TRUE(command(a, cp::SetReturnMute{false}));
    EXPECT_EQ(peers_.return_mutes().at(1).epoch, 2U);
}

TEST_F(HostSessionTest, ReturnMuteDoesNotAffectTheChannelOrOtherParticipants) {
    create();
    const auto a = connect(1, "Vo");
    const auto b = connect(2, "Gt");
    ASSERT_TRUE(command(a, cp::SetReturnMute{false}));
    EXPECT_TRUE(strip(a).channel_muted);
    EXPECT_TRUE(mixer_.channels().at(a).muted);
    EXPECT_TRUE(strip(b).return_muted);
    EXPECT_TRUE(peers_.return_mutes().at(2).send_muted);
}

TEST_F(HostSessionTest, SameReturnMuteIsAcknowledgedWithoutChanges) {
    create();
    const auto a = connect(1, "Vo");
    const auto before = strip(a).revision;
    const auto sent_before = peers_.sent_to(1).size();
    EXPECT_EQ(command(a, cp::SetReturnMute{true}), before);
    EXPECT_EQ(peers_.sent_to(1).size(), sent_before);
}

TEST_F(HostSessionTest, ReturnMuteTargetsOnlyChannelStrips) {
    create();
    EXPECT_FALSE(command("master", cp::SetReturnMute{false}));
    EXPECT_EQ(last_reject_, ErrorCode::kInvalidState);
    request_join(7, "Tp");
    EXPECT_FALSE(command("pending1", cp::SetReturnMute{false}));
    EXPECT_EQ(last_reject_, ErrorCode::kInvalidState);
}

TEST_F(HostSessionTest, ReturnStateIsRestoredOnTheNewConnectionAfterReconnect) {
    create();
    const auto a = connect(1, "Vo");
    const auto token = approved_token(1);
    ASSERT_TRUE(token);
    ASSERT_TRUE(command(a, cp::SetReturnMute{false}));
    session().on_peer_timeout(1);
    // 再接続の間に操作されたら、接続がなくても状態は変わり、戻ったときに反映する。
    ASSERT_TRUE(command(a, cp::SetReturnMute{true}));
    ASSERT_TRUE(command(a, cp::SetReturnMute{false}));

    send_from(20, wire::ReconnectRequest{*token});
    EXPECT_EQ(approved_token(20), token);
    ASSERT_TRUE(peers_.return_mutes().contains(20));
    EXPECT_FALSE(peers_.return_mutes().at(20).send_muted);
    // 世代番号は接続をまたいで続く。
    EXPECT_EQ(peers_.return_mutes().at(20).epoch, 2U);
    EXPECT_EQ(last_return_notice(peers_, 20), (wire::ReturnMuteNotice{false, 2}));
}

// --- プラグインの状態と一覧 ---

TEST_F(HostSessionTest, HelloSendsThePluginStateAndCatalogPagesAfterTheSnapshot) {
    std::vector<PluginDescription> many;
    for (int i = 0; i < 30; ++i) {
        PluginDescription d;
        d.name = "P" + std::to_string(i);
        d.identifier = "VST3-P-" + std::to_string(i);
        many.push_back(d);
    }
    session().set_available_plugins(many);
    ui_.clear();

    session().handle_ui_message(cp::Hello{});

    const auto& messages = ui_.messages();
    ASSERT_EQ(messages.size(), 4U);
    EXPECT_TRUE(std::holds_alternative<cp::MixerSnapshotEvent>(messages[0]));
    const auto& plugins = std::get<cp::PluginsUpdated>(messages[1]).plugins;
    EXPECT_EQ(plugins.entity_id, "plugins");
    EXPECT_EQ(plugins.catalog_size, 30U);
    const auto& first = std::get<cp::PluginCatalogPage>(messages[2]);
    const auto& second = std::get<cp::PluginCatalogPage>(messages[3]);
    EXPECT_EQ(first.generation, plugins.catalog_generation);
    EXPECT_EQ(first.offset, 0U);
    EXPECT_EQ(first.plugins.size(), cp::kMaxPluginCatalogPageEntries);
    EXPECT_EQ(second.offset, cp::kMaxPluginCatalogPageEntries);
    EXPECT_EQ(second.plugins.size(), 30U - cp::kMaxPluginCatalogPageEntries);
    EXPECT_EQ(second.plugins.back().plugin_id, "VST3-P-29");
}

TEST_F(HostSessionTest, NewCatalogsAdvanceTheGenerationAndLeaveOutOversizedPlugins) {
    session().set_available_plugins(insert_plugins());
    const auto generation = ui_.last<cp::PluginsUpdated>()->plugins.catalog_generation;
    auto plugins = insert_plugins();
    plugins[1].identifier = std::string(cp::kMaxPluginIdBytes + 1, 'x');

    session().set_available_plugins(plugins);

    const auto info = ui_.last<cp::PluginsUpdated>()->plugins;
    EXPECT_EQ(info.catalog_generation, generation + 1);
    EXPECT_EQ(info.catalog_size, 1U);
    const auto page = ui_.last<cp::PluginCatalogPage>();
    ASSERT_TRUE(page);
    ASSERT_EQ(page->plugins.size(), 1U);
    EXPECT_EQ(page->plugins[0].plugin_id, "VST3-EQ-1");
}

TEST_F(HostSessionTest, ReportsBlockedPluginFilesWithShortenedLongPaths) {
    const std::string long_dir(cp::kMaxTextBytes, 'd');
    std::vector<PluginScanRecord> blocked{
        {{"C:/VST3/Broken.vst3", 1, 1}, PluginScanStatus::kCrashed, {}},
        {{"C:/" + long_dir + "/Slow.vst3", 1, 1}, PluginScanStatus::kTimedOut, {}},
        {{"C:/VST3/Fine.vst3", 1, 1}, PluginScanStatus::kScanned, {}},
    };

    session().set_available_plugins({}, blocked);

    const auto info = ui_.last<cp::PluginsUpdated>()->plugins;
    ASSERT_EQ(info.blocked.size(), 2U);
    EXPECT_EQ(info.blocked[0],
              (cp::BlockedPluginFile{"C:/VST3/Broken.vst3", cp::BlockedPluginReason::kCrashed}));
    EXPECT_EQ(info.blocked[1],
              (cp::BlockedPluginFile{"Slow.vst3", cp::BlockedPluginReason::kTimedOut}));
}

TEST_F(HostSessionTest, ReportsTheStateOfEachInsert) {
    session().set_available_plugins(insert_plugins());
    create();
    const auto a = connect(1, "Vo");
    ASSERT_TRUE(command("routing", cp::StageRouting{insert_routing(a)}));
    ASSERT_TRUE(command("routing", cp::CommitRouting{false}));
    auto inserts = ui_.last<cp::PluginsUpdated>()->plugins.inserts;
    ASSERT_EQ(inserts.size(), 2U);
    EXPECT_EQ(inserts[0], (cp::PluginInsertStatus{"fx-eq", cp::PluginInsertState::kLoading, {}}));

    session().on_plugin_loaded("fx-eq", plugins_.loads()[0].ticket, fake_processor());
    session().on_plugin_loaded("fx-verb", plugins_.loads()[1].ticket,
                               common::make_unexpected(PluginLoadError::kInstantiateFailed));
    inserts = ui_.last<cp::PluginsUpdated>()->plugins.inserts;
    EXPECT_EQ(inserts[0], (cp::PluginInsertStatus{"fx-eq", cp::PluginInsertState::kReady, {}}));
    EXPECT_EQ(inserts[1], (cp::PluginInsertStatus{"fx-verb", cp::PluginInsertState::kFailed,
                                                  cp::PluginInsertFailure::kInstantiateFailed}));

    mixer_.plugins()[0].fault->raise();
    session().tick();
    inserts = ui_.last<cp::PluginsUpdated>()->plugins.inserts;
    EXPECT_EQ(inserts[0], (cp::PluginInsertStatus{"fx-eq", cp::PluginInsertState::kFailed,
                                                  cp::PluginInsertFailure::kProcessingFailed}));
}

TEST_F(HostSessionTest, RescanAsksForAScanAndShowsScanningAtOnce) {
    session().handle_ui_message(cp::Hello{});

    EXPECT_EQ(command("plugins", cp::RescanPlugins{}), cp::Revision{1});
    EXPECT_EQ(plugin_scan_.rescans(), 1);
    EXPECT_TRUE(ui_.last<cp::PluginsUpdated>()->plugins.scanning);

    session().set_plugin_scanning(false);
    EXPECT_FALSE(ui_.last<cp::PluginsUpdated>()->plugins.scanning);
    EXPECT_FALSE(command("routing", cp::RescanPlugins{}));
    EXPECT_EQ(last_reject_, cp::ErrorCode::kInvalidState);
}

TEST_F(HostSessionTest, OpensTheEditorOnlyForALoadedInsert) {
    session().set_available_plugins(insert_plugins());
    create();
    const auto a = connect(1, "Vo");
    ASSERT_TRUE(command("routing", cp::StageRouting{insert_routing(a)}));
    ASSERT_TRUE(command("routing", cp::CommitRouting{false}));

    EXPECT_FALSE(command("plugins", cp::OpenPluginEditor{"fx-none"}));
    EXPECT_EQ(last_reject_, cp::ErrorCode::kNotFound);
    // 読み込み中は開けない。
    EXPECT_FALSE(command("plugins", cp::OpenPluginEditor{"fx-eq"}));
    EXPECT_EQ(last_reject_, cp::ErrorCode::kInvalidState);

    session().on_plugin_loaded("fx-eq", plugins_.loads()[0].ticket, fake_processor());
    EXPECT_TRUE(command("plugins", cp::OpenPluginEditor{"fx-eq"}));
    ASSERT_EQ(plugins_.editors().size(), 1U);
    EXPECT_EQ(plugins_.editors()[0].insert_id, "fx-eq");
    EXPECT_EQ(plugins_.editors()[0].title, "EQ");

    session().on_plugin_editor_failed("fx-eq");
    const auto notice = ui_.last<cp::Notice>();
    ASSERT_TRUE(notice);
    EXPECT_EQ(notice->code, "plugin_editor_failed");
    EXPECT_EQ(notice->params.at("name"), "EQ");
}

}  // namespace
}  // namespace vocroz::session
