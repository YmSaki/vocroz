// SPDX-License-Identifier: AGPL-3.0-only
//
// 子機のセッション管理のテスト。
// 初期ミュート、招待の解決から接続まで、再接続トークンによる同じストリップへの復帰、
// 入力デバイスの欠落を、ポートのフェイクで OS なしに確かめる。

#include "vocroz/session/client_session.h"

#include <chrono>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include <gtest/gtest.h>

#include "fakes/fake_capture_control.h"
#include "fakes/fake_clock.h"
#include "fakes/fake_host_link.h"
#include "fakes/fake_logger.h"
#include "fakes/fake_rendezvous_client.h"
#include "fakes/fake_settings_store.h"
#include "fakes/fake_ui_event_sink.h"
#include "wire/peer_message_codec.h"

namespace vocroz::session {
namespace {

namespace cp = control_protocol;
using cp::ClientState;
using cp::ErrorCode;
using namespace std::chrono_literals;

class ClientSessionTest : public ::testing::Test {
protected:
    ClientSessionTest() {
        resolved_.endpoint.host_public_key.fill(0xAB);
        resolved_.endpoint.rendezvous_id.fill(0x11);
        resolved_.invite_token.fill(0x22);
    }

    ClientSession& session() {
        if (!session_) {
            session_.emplace(ClientSession::Ports{ui_, capture_, link_, rendezvous_, settings_,
                                                  clock_, logger_});
            session_->start();
        }
        return *session_;
    }

    std::optional<cp::Revision> command(cp::DiscreteCommandBody body,
                                        std::optional<cp::Revision> expected = std::nullopt,
                                        const std::string& entity_id = "client") {
        const auto request_id = "r" + std::to_string(++request_number_);
        const auto revision = expected ? *expected : session().snapshot().revision;
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

    ClientState state() {
        return session().snapshot().client_state;
    }

    void receive(const wire::HostToClientMessage& message) {
        session().on_link_message(wire::encode(message));
    }

    // 子機が最後に送ったメッセージ。
    std::optional<wire::ClientToHostMessage> last_sent() const {
        if (link_.sent().empty()) {
            return std::nullopt;
        }
        auto decoded = wire::decode_client_to_host(link_.sent().back());
        EXPECT_TRUE(decoded);
        return decoded ? std::optional{decoded.value()} : std::nullopt;
    }

    template <typename T>
    std::optional<T> last_sent_of() const {
        for (auto it = link_.sent().rbegin(); it != link_.sent().rend(); ++it) {
            auto decoded = wire::decode_client_to_host(*it);
            if (decoded) {
                if (const auto* m = std::get_if<T>(&decoded.value())) {
                    return *m;
                }
            }
        }
        return std::nullopt;
    }

    // 招待の解決から、承認と経路の確立までを進める。
    void join_and_connect(const ReconnectToken& token) {
        ASSERT_TRUE(command(cp::Join{"ABCDE-12345"}));
        session().on_invite_resolved(parse_invite_code("ABCDE12345").value(), resolved_);
        session().on_link_established();
        receive(wire::JoinPending{"日曜リハーサル"});
        receive(wire::JoinApproved{token});
        session().on_media_established();
    }

    static ReconnectToken token(std::uint8_t value) {
        ReconnectToken t{};
        t.fill(value);
        return t;
    }

    FakeUiEventSink ui_;
    FakeCaptureControl capture_;
    FakeHostLink link_;
    FakeRendezvousClient rendezvous_;
    FakeSettingsStore settings_;
    common::FakeClock clock_;
    common::FakeLogger logger_;
    ResolvedInvite resolved_;
    std::optional<ClientSession> session_;
    int request_number_ = 0;
    std::optional<ErrorCode> last_reject_;
};

// --- 初期ミュートと設定 ---

TEST_F(ClientSessionTest, StartsSendMutedAndDisconnected) {
    settings_.client() = ClientSettings{"Vo 佐藤", "mic-1", -6.0};
    session();
    EXPECT_TRUE(capture_.send_muted());
    EXPECT_EQ(capture_.input(), "mic-1");
    EXPECT_EQ(capture_.level_db(), -6.0);
    const auto s = session().snapshot();
    EXPECT_EQ(s.client_state, ClientState::kDisconnected);
    EXPECT_TRUE(s.send_muted);
    EXPECT_EQ(s.selected_input_id, "mic-1");
}

TEST_F(ClientSessionTest, StartsSendMutedWithDefaultsWhenSettingsAreCorrupted) {
    settings_.fail_loads_with(SettingsError::kCorrupted);
    session();
    EXPECT_TRUE(capture_.send_muted());
    EXPECT_EQ(session().snapshot().selected_input_id, std::nullopt);
    EXPECT_EQ(session().snapshot().send_level_db, 0.0);
}

TEST_F(ClientSessionTest, SelectingAMicrophoneDoesNotStartSending) {
    ASSERT_TRUE(command(cp::SelectInput{"mic-2"}));
    EXPECT_EQ(capture_.input(), "mic-2");
    EXPECT_TRUE(capture_.send_muted());
    EXPECT_TRUE(session().snapshot().send_muted);
    EXPECT_EQ(settings_.client()->selected_input_id, "mic-2");
}

// --- 参加の流れ ---

TEST_F(ClientSessionTest, JoinResolvesTheInviteConnectsAndWaitsForApproval) {
    ASSERT_TRUE(command(cp::Join{"https://vocroz.example/join#abcde-12345"}));
    EXPECT_EQ(state(), ClientState::kResolving);
    ASSERT_EQ(rendezvous_.resolutions().size(), 1U);
    EXPECT_EQ(rendezvous_.resolutions().front().normalized(), "ABCDE12345");

    session().on_invite_resolved(rendezvous_.resolutions().front(), resolved_);
    EXPECT_EQ(state(), ClientState::kAuthenticating);
    EXPECT_EQ(link_.connected_to(), resolved_.endpoint);

    session().on_link_established();
    const auto join = last_sent_of<wire::JoinRequest>();
    ASSERT_TRUE(join);
    EXPECT_EQ(join->invite_token, resolved_.invite_token);
    EXPECT_EQ(join->reconnect_token, std::nullopt);

    receive(wire::JoinPending{"日曜リハーサル"});
    EXPECT_EQ(state(), ClientState::kWaitingApproval);
    EXPECT_EQ(session().snapshot().session_name, "日曜リハーサル");

    receive(wire::JoinApproved{token(1)});
    EXPECT_EQ(state(), ClientState::kConnecting);
    // 承認されたら、今の送信ミュートと入力の状態を親機へ伝える。
    const auto mute = last_sent_of<wire::SendMuteReport>();
    ASSERT_TRUE(mute);
    EXPECT_TRUE(mute->send_muted);

    session().on_media_established();
    EXPECT_EQ(state(), ClientState::kConnected);
}

TEST_F(ClientSessionTest, ConnectionProgressDoesNotAdvanceTheRevision) {
    ASSERT_TRUE(command(cp::Join{"ABCDE-12345"}));
    const auto revision = session().snapshot().revision;
    session().on_invite_resolved(parse_invite_code("ABCDE12345").value(), resolved_);
    session().on_link_established();
    receive(wire::JoinPending{"x"});
    EXPECT_EQ(session().snapshot().revision, revision);
    // 接続処理の途中でも、表示中の revision で送信ミュートを操作できる。
    EXPECT_TRUE(command(cp::SetSendMute{true}, revision));
}

TEST_F(ClientSessionTest, RejectsMalformedInvitesWithoutChangingState) {
    EXPECT_FALSE(command(cp::Join{"not an invite"}));
    EXPECT_EQ(last_reject_, ErrorCode::kInviteInvalid);
    EXPECT_EQ(state(), ClientState::kDisconnected);
    EXPECT_TRUE(rendezvous_.resolutions().empty());
}

TEST_F(ClientSessionTest, ResolutionFailureEndsInFailedWithTheCause) {
    ASSERT_TRUE(command(cp::Join{"ABCDE-12345"}));
    session().on_invite_resolved(parse_invite_code("ABCDE12345").value(),
                                 common::make_unexpected(ErrorCode::kInviteExpired));
    EXPECT_EQ(state(), ClientState::kFailed);
    EXPECT_EQ(session().snapshot().failure_code, ErrorCode::kInviteExpired);
    // FAILED からは参加し直せる。
    EXPECT_TRUE(command(cp::Join{"ABCDE-12345"}));
    EXPECT_EQ(session().snapshot().failure_code, std::nullopt);
}

TEST_F(ClientSessionTest, IgnoresAResolutionForAnAbandonedJoin) {
    ASSERT_TRUE(command(cp::Join{"ABCDE-12345"}));
    ASSERT_TRUE(command(cp::Leave{}));
    session().on_invite_resolved(parse_invite_code("ABCDE12345").value(), resolved_);
    EXPECT_EQ(state(), ClientState::kDisconnected);
    EXPECT_FALSE(link_.connected_to());
}

TEST_F(ClientSessionTest, RejectionFailsWithRejected) {
    ASSERT_TRUE(command(cp::Join{"ABCDE-12345"}));
    session().on_invite_resolved(parse_invite_code("ABCDE12345").value(), resolved_);
    session().on_link_established();
    receive(wire::JoinDenied{ErrorCode::kRejected});
    EXPECT_EQ(state(), ClientState::kFailed);
    EXPECT_EQ(session().snapshot().failure_code, ErrorCode::kRejected);
    EXPECT_FALSE(link_.open());
}

TEST_F(ClientSessionTest, UnreachableHostFailsWithUnreachable) {
    ASSERT_TRUE(command(cp::Join{"ABCDE-12345"}));
    session().on_invite_resolved(parse_invite_code("ABCDE12345").value(), resolved_);
    session().on_link_failed(ErrorCode::kUnreachable);
    EXPECT_EQ(state(), ClientState::kFailed);
    EXPECT_EQ(session().snapshot().failure_code, ErrorCode::kUnreachable);
}

TEST_F(ClientSessionTest, RejectsMixerCommandsAndJoinWhileConnected) {
    EXPECT_FALSE(command(cp::SetMasterMute{false}));
    EXPECT_EQ(last_reject_, ErrorCode::kInvalidState);
    EXPECT_FALSE(command(cp::SetSendMute{false}, std::nullopt, "master"));
    EXPECT_EQ(last_reject_, ErrorCode::kNotFound);
    join_and_connect(token(1));
    EXPECT_FALSE(command(cp::Join{"ABCDE-12345"}));
    EXPECT_EQ(last_reject_, ErrorCode::kInvalidState);
}

// --- 送信ミュート ---

TEST_F(ClientSessionTest, UnmutingAdvancesTheEpochAndNotifiesTheHost) {
    join_and_connect(token(1));
    ASSERT_TRUE(command(cp::SetSendMute{false}));
    EXPECT_FALSE(capture_.send_muted());
    EXPECT_EQ(capture_.epoch(), 1U);
    const auto report = last_sent_of<wire::SendMuteReport>();
    ASSERT_TRUE(report);
    EXPECT_FALSE(report->send_muted);
    EXPECT_EQ(report->epoch, 1U);

    ASSERT_TRUE(command(cp::SetSendMute{true}));
    ASSERT_TRUE(command(cp::SetSendMute{false}));
    EXPECT_EQ(capture_.epoch(), 2U);
}

TEST_F(ClientSessionTest, JoiningAgainStartsSendMuted) {
    join_and_connect(token(1));
    ASSERT_TRUE(command(cp::SetSendMute{false}));
    ASSERT_TRUE(command(cp::Leave{}));
    EXPECT_TRUE(capture_.send_muted());
    EXPECT_TRUE(session().snapshot().send_muted);
    EXPECT_TRUE(std::holds_alternative<wire::LeaveNotice>(*last_sent()));
}

TEST_F(ClientSessionTest, SendLevelIsClampedAndDoesNotAdvanceTheRevision) {
    const auto revision = session().snapshot().revision;
    session().handle_ui_message(cp::SetGain{"client", 1, 20.0});
    EXPECT_EQ(capture_.level_db(), 12.0);
    session().handle_ui_message(cp::SetGain{"client", 2, std::nullopt});
    EXPECT_EQ(capture_.level_db(), -60.0);
    session().handle_ui_message(cp::SetGain{"client", 1, 0.0});
    EXPECT_EQ(capture_.level_db(), -60.0);
    EXPECT_EQ(session().snapshot().revision, revision);
    EXPECT_EQ(ui_.all<cp::GainAck>().size(), 2U);
}

// --- 入力の構成（docs/adr/0035-client-input-expansion.md） ---

TEST_F(ClientSessionTest, StartsWithAMonoMicrophoneAndNoAppAudio) {
    session();
    ASSERT_EQ(capture_.streams().size(), 1U);
    EXPECT_EQ(capture_.streams()[0], (InputStream{0, cp::InputSourceKind::kMicrophone, 1}));
    EXPECT_EQ(session().snapshot().input_channels, 1);
    EXPECT_FALSE(session().snapshot().app_audio);
}

TEST_F(ClientSessionTest, RestoresTheSavedChannelCountButNotTheAppAudio) {
    ClientSettings saved{"Vo", "mic-1", 0.0};
    saved.input_channels = 2;
    settings_.client() = saved;
    session();
    EXPECT_EQ(session().snapshot().input_channels, 2);
    EXPECT_EQ(capture_.streams(),
              (std::vector<InputStream>{{0, cp::InputSourceKind::kMicrophone, 2}}));
}

TEST_F(ClientSessionTest, DeclaresTheInputsRightAfterTheJoinRequest) {
    ASSERT_TRUE(command(cp::SetInputChannels{2}));
    ASSERT_TRUE(command(cp::Join{"ABCDE-12345"}));
    session().on_invite_resolved(parse_invite_code("ABCDE12345").value(), resolved_);
    session().on_link_established();
    ASSERT_GE(link_.sent().size(), 2U);
    const auto request = wire::decode_client_to_host(link_.sent()[link_.sent().size() - 2]).value();
    EXPECT_TRUE(std::holds_alternative<wire::JoinRequest>(request));
    const auto sent = last_sent();
    ASSERT_TRUE(sent);
    const auto* report = std::get_if<wire::StreamsReport>(&*sent);
    ASSERT_NE(report, nullptr);
    EXPECT_EQ(report->streams,
              (std::vector<InputStream>{{0, cp::InputSourceKind::kMicrophone, 2}}));
}

TEST_F(ClientSessionTest, SwitchingToStereoReportsTheInputsAndSavesTheChannelCount) {
    join_and_connect(token(1));
    ASSERT_TRUE(command(cp::SetSendMute{false}));
    const auto revision = session().snapshot().revision;
    const auto applied = command(cp::SetInputChannels{2});
    ASSERT_TRUE(applied);
    EXPECT_GT(*applied, revision);
    EXPECT_EQ(capture_.streams(),
              (std::vector<InputStream>{{0, cp::InputSourceKind::kMicrophone, 2}}));
    const auto sent = last_sent();
    ASSERT_TRUE(sent);
    const auto* report = std::get_if<wire::StreamsReport>(&*sent);
    ASSERT_NE(report, nullptr);
    EXPECT_EQ(report->streams, capture_.streams());
    EXPECT_EQ(settings_.client()->input_channels, 2);
    // 送信ミュートはそのまま。親機では作り直したストリップがチャンネルミュートで始まる。
    EXPECT_FALSE(capture_.send_muted());
}

TEST_F(ClientSessionTest, AppAudioAddsAStereoStreamWithoutBeingSaved) {
    join_and_connect(token(1));
    ASSERT_TRUE(command(cp::SetAppAudio{true}));
    const std::vector<InputStream> expected{
        {0, cp::InputSourceKind::kMicrophone, 1},
        {1, cp::InputSourceKind::kApplicationAudio, 2},
    };
    EXPECT_EQ(capture_.streams(), expected);
    EXPECT_EQ(last_sent_of<wire::StreamsReport>()->streams, expected);
    EXPECT_TRUE(session().snapshot().app_audio);

    ASSERT_TRUE(command(cp::SetAppAudio{false}));
    EXPECT_EQ(capture_.streams().size(), 1U);
    EXPECT_EQ(last_sent_of<wire::StreamsReport>()->streams.size(), 1U);
}

TEST_F(ClientSessionTest, ChangingInputsWhileDisconnectedSendsNothing) {
    ASSERT_TRUE(command(cp::SetAppAudio{true}));
    ASSERT_TRUE(command(cp::SetInputChannels{2}));
    EXPECT_TRUE(link_.sent().empty());
    EXPECT_EQ(capture_.streams().size(), 2U);
}

TEST_F(ClientSessionTest, ReconnectingDeclaresTheInputsAgain) {
    join_and_connect(token(1));
    ASSERT_TRUE(command(cp::SetAppAudio{true}));
    session().on_link_lost();
    link_.clear_sent();
    session().on_link_established();
    receive(wire::JoinApproved{token(1)});
    const auto report = last_sent_of<wire::StreamsReport>();
    ASSERT_TRUE(report);
    EXPECT_EQ(report->streams.size(), 2U);
}

// --- 再接続（AT-A05 のドメイン部分） ---

TEST_F(ClientSessionTest, LinkLossReconnectsWithTheTokenToTheSameStrip) {
    join_and_connect(token(7));
    ASSERT_TRUE(command(cp::SetSendMute{false}));
    session().on_link_lost();
    EXPECT_EQ(state(), ClientState::kReconnecting);
    EXPECT_EQ(link_.reconnect_count(), 1);

    link_.clear_sent();
    session().on_link_established();
    const auto request = last_sent_of<wire::ReconnectRequest>();
    ASSERT_TRUE(request);
    EXPECT_EQ(request->reconnect_token, token(7));

    receive(wire::JoinApproved{token(7)});
    EXPECT_EQ(state(), ClientState::kConnected);
    // 復帰したら、解除済みの送信ミュートを親機へ伝え直す。
    const auto report = last_sent_of<wire::SendMuteReport>();
    ASSERT_TRUE(report);
    EXPECT_FALSE(report->send_muted);
}

TEST_F(ClientSessionTest, ResumedLinkReturnsToConnected) {
    join_and_connect(token(7));
    session().on_link_lost();
    session().on_link_resumed();
    EXPECT_EQ(state(), ClientState::kConnected);
}

TEST_F(ClientSessionTest, GivesUpAfterTheGracePeriodButKeepsTheTokenForRejoining) {
    join_and_connect(token(7));
    session().on_link_lost();
    clock_.advance(119s);
    session().tick();
    EXPECT_EQ(state(), ClientState::kReconnecting);
    clock_.advance(1s);
    session().tick();
    EXPECT_EQ(state(), ClientState::kFailed);
    EXPECT_EQ(session().snapshot().failure_code, ErrorCode::kReconnectTimeout);
    EXPECT_FALSE(link_.open());

    // 同じ親機の招待で参加し直すと、トークンを添えて LOST のストリップへ戻る。
    ASSERT_TRUE(command(cp::Join{"ABCDE-12345"}));
    session().on_invite_resolved(parse_invite_code("ABCDE12345").value(), resolved_);
    session().on_link_established();
    const auto join = last_sent_of<wire::JoinRequest>();
    ASSERT_TRUE(join);
    EXPECT_EQ(join->reconnect_token, token(7));
    receive(wire::JoinApproved{token(7)});
    EXPECT_EQ(state(), ClientState::kConnecting);
}

TEST_F(ClientSessionTest, DoesNotOfferTheTokenToADifferentHost) {
    join_and_connect(token(7));
    ASSERT_TRUE(command(cp::Leave{}));
    resolved_.endpoint.host_public_key.fill(0xCD);
    ASSERT_TRUE(command(cp::Join{"ABCDE-12345"}));
    session().on_invite_resolved(parse_invite_code("ABCDE12345").value(), resolved_);
    session().on_link_established();
    EXPECT_EQ(last_sent_of<wire::JoinRequest>()->reconnect_token, std::nullopt);
}

TEST_F(ClientSessionTest, LinkLossBeforeApprovalFailsAsUnreachable) {
    ASSERT_TRUE(command(cp::Join{"ABCDE-12345"}));
    session().on_invite_resolved(parse_invite_code("ABCDE12345").value(), resolved_);
    session().on_link_established();
    receive(wire::JoinPending{"x"});
    session().on_link_lost();
    EXPECT_EQ(state(), ClientState::kFailed);
    EXPECT_EQ(session().snapshot().failure_code, ErrorCode::kUnreachable);
    EXPECT_EQ(link_.reconnect_count(), 0);
}

TEST_F(ClientSessionTest, SessionEndReturnsToDisconnectedWithTheReasonAndDropsTheToken) {
    join_and_connect(token(7));
    ASSERT_TRUE(command(cp::SetSendMute{false}));
    receive(wire::SessionEndedNotice{});
    EXPECT_EQ(state(), ClientState::kDisconnected);
    EXPECT_EQ(session().snapshot().failure_code, ErrorCode::kSessionEnded);
    EXPECT_TRUE(capture_.send_muted());
    EXPECT_FALSE(link_.open());

    ASSERT_TRUE(command(cp::Join{"ABCDE-12345"}));
    session().on_invite_resolved(parse_invite_code("ABCDE12345").value(), resolved_);
    session().on_link_established();
    EXPECT_EQ(last_sent_of<wire::JoinRequest>()->reconnect_token, std::nullopt);
}

TEST_F(ClientSessionTest, IgnoresMalformedOrMisdirectedMessages) {
    join_and_connect(token(7));
    const auto before = session().snapshot();
    session().on_link_message(wire::encode(wire::ClientToHostMessage{wire::LeaveNotice{}}));
    const std::vector<std::uint8_t> garbage{0x83};
    session().on_link_message(garbage);
    EXPECT_EQ(session().snapshot(), before);
}

// --- 入力デバイスの欠落 ---

TEST_F(ClientSessionTest, UnpluggedMicrophoneRaisesInputMissingWithoutChangingTheMute) {
    session().on_input_devices_changed({{"mic-1", "USB マイク"}});
    ASSERT_TRUE(command(cp::SelectInput{"mic-1"}));
    join_and_connect(token(7));
    ASSERT_TRUE(command(cp::SetSendMute{false}));
    const auto revision = session().snapshot().revision;

    session().on_input_devices_changed({});
    const auto s = session().snapshot();
    EXPECT_TRUE(s.input_missing);
    EXPECT_FALSE(s.send_muted);
    EXPECT_EQ(s.revision, revision);  // 観測値なので revision は進めない
    EXPECT_EQ(ui_.last<cp::Notice>()->code, "input_missing");
    const auto report = last_sent_of<wire::InputReport>();
    ASSERT_TRUE(report);
    EXPECT_TRUE(report->input_missing);

    session().on_input_devices_changed({{"mic-1", "USB マイク"}});
    EXPECT_FALSE(session().snapshot().input_missing);
    EXPECT_FALSE(last_sent_of<wire::InputReport>()->input_missing);
}

TEST_F(ClientSessionTest, ChoosingNoInputIsNotReportedAsMissing) {
    session().on_input_devices_changed({{"mic-1", "USB マイク"}});
    ASSERT_TRUE(command(cp::SelectInput{std::nullopt}));
    EXPECT_FALSE(session().snapshot().input_missing);
    EXPECT_EQ(capture_.input(), std::nullopt);
}

TEST_F(ClientSessionTest, ReplaysTheFullStateToANewlyConnectedUi) {
    session().on_input_devices_changed({{"mic-1", "USB マイク"}});
    session().handle_ui_message(cp::Hello{});
    const auto snapshot = ui_.last<cp::ClientSnapshotEvent>();
    ASSERT_TRUE(snapshot);
    EXPECT_EQ(snapshot->snapshot, session().snapshot());
}

}  // namespace
}  // namespace vocroz::session
