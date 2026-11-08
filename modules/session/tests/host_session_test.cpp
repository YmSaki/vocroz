// SPDX-License-Identifier: AGPL-3.0-only
//
// 親機のセッション管理のテスト。
// 初期ミュート、承認前の遮断、切断からの同じストリップへの復帰、招待と権限の検証、
// コマンドの revision の規則を、ポートのフェイクで OS なしに確かめる。

#include "vocroz/session/host_session.h"

#include <chrono>
#include <optional>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "fakes/fake_clock.h"
#include "fakes/fake_logger.h"
#include "fakes/fake_mixer_control.h"
#include "fakes/fake_peer_link.h"
#include "fakes/fake_rendezvous_client.h"
#include "fakes/fake_secure_random.h"
#include "fakes/fake_settings_store.h"
#include "fakes/fake_ui_event_sink.h"
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
            session_.emplace(config_, HostSession::Ports{ui_, mixer_, peers_, rendezvous_,
                                                         settings_, random_, clock_, logger_});
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
    FakePeerLink peers_;
    FakeRendezvousClient rendezvous_;
    FakeSettingsStore settings_;
    FakeSecureRandom random_;
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
    settings_.host() = HostSettings{"日曜リハーサル", "dev-1", -6.0};
    session();
    EXPECT_TRUE(mixer_.master_muted());
    EXPECT_EQ(mixer_.master_gain_db(), cp::GainDb{-6.0});
    EXPECT_EQ(session().snapshot().master.gain_db, cp::GainDb{-6.0});
}

// --- セッションの作成と招待 ---

TEST_F(HostSessionTest, CreatingASessionRegistersAnInviteAndBecomesReady) {
    settings_.host() = HostSettings{"日曜リハーサル", std::nullopt, 0.0};
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

// --- 出力デバイスの抜去 ---

TEST_F(HostSessionTest, LosingTheOutputDeviceSafetyMutesTheMaster) {
    session().on_output_device_opened("dev-1", "スピーカー");
    ASSERT_TRUE(command("master", cp::SetMasterMute{false}));
    EXPECT_FALSE(mixer_.master_muted());
    const auto stale = revision_of("master");

    session().on_output_device_lost();
    const auto s = session().snapshot();
    EXPECT_EQ(s.session.output_device_state, cp::OutputDeviceState::kLost);
    EXPECT_TRUE(s.master.master_muted);
    EXPECT_EQ(s.master.master_mute_reason, cp::MasterMuteReason::kSafety);
    EXPECT_TRUE(mixer_.master_muted());
    EXPECT_EQ(ui_.last<cp::Notice>()->code, "output_lost");

    // ミュートされたことを知らないまま押した解除は拒否する。
    EXPECT_FALSE(command("master", cp::SetMasterMute{false}, stale));
    EXPECT_EQ(last_reject_, ErrorCode::kRevisionConflict);
    EXPECT_TRUE(mixer_.master_muted());

    // 新しいデバイスを開いても、ミュートはオペレーターが解除するまで続く。
    session().on_output_device_opened("dev-2", "USB Audio");
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

}  // namespace
}  // namespace vocroz::session
