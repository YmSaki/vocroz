// SPDX-License-Identifier: AGPL-3.0-only
//
// 親機のセッションの SRTP 外部出力の単体テスト（docs/adr/0040-srtp-external-output.md）。
// 起動時は止めた状態から始めること、宛先と鍵がそろうまで送らないこと、設定と鍵は止めている
// 間だけ変えられること、鍵は保存するが UI には返さないこと、送り出すバスが消えたら止めること、
// 接続テストの結果を返すことを確かめる。

#include <optional>
#include <string>
#include <variant>

#include <gtest/gtest.h>

#include "vocroz/session/host_session.h"
#include "vocroz/transport/srtp_master_key.h"

#include "fakes/fake_clock.h"
#include "fakes/fake_external_output_control.h"
#include "fakes/fake_logger.h"
#include "fakes/fake_mixer_control.h"
#include "fakes/fake_output_device_control.h"
#include "fakes/fake_peer_link.h"
#include "fakes/fake_playout_control.h"
#include "fakes/fake_plugin_rack.h"
#include "fakes/fake_rendezvous_client.h"
#include "fakes/fake_secret_store.h"
#include "fakes/fake_secure_random.h"
#include "fakes/fake_settings_store.h"
#include "fakes/fake_ui_event_sink.h"

namespace vocroz::session {
namespace {

namespace cp = control_protocol;
using cp::ErrorCode;
using cp::ExternalOutputResult;
using cp::ExternalOutputState;

// テスト用の鍵（1 から 30 のバイト列）。受信機の設定画面に貼り付ける形式。
constexpr const char* kKey = "inline:AQIDBAUGBwgJCgsMDQ4PEBESExQVFhcYGRobHB0e";
constexpr const char* kEntity = "external_output";

cp::ExternalOutputConfig destination(std::string source_id = "master") {
    cp::ExternalOutputConfig config;
    config.host = "192.0.2.10";
    config.port = 5004;
    config.source_id = std::move(source_id);
    config.profile = cp::ExternalOutputProfile::kOpusMono;
    config.payload_type = 100;
    config.crypto_suite = cp::SrtpSuite::kAesCm128HmacSha1_32;
    return config;
}

cp::RoutingConfig bus_routing() {
    cp::RoutingConfig config;
    config.buses.push_back(cp::RoutingBus{"bus-stream", "配信", cp::BusKind::kUser});
    config.sends.push_back(
        cp::RoutingSend{"send-1", "master", "bus-stream", cp::SendTap::kPostFader});
    return config;
}

class HostExternalOutputTest : public ::testing::Test {
protected:
    HostSession& session() {
        if (!session_) {
            session_.emplace(HostSession::Config{},
                             HostSession::Ports{ui_, mixer_, outputs_, peers_, rendezvous_,
                                                settings_, random_, clock_, logger_, playout_,
                                                external_output_, secrets_, plugins_});
            session_->start();
        }
        return *session_;
    }

    cp::ExternalOutput output() {
        return session().snapshot().external_output;
    }

    // 外部出力への離散コマンドを送り、Ack なら revision、Reject なら std::nullopt を返す。
    std::optional<cp::Revision> command(const std::string& entity_id,
                                        cp::DiscreteCommandBody body) {
        const auto request_id = "r" + std::to_string(++request_number_);
        const auto snapshot = session().snapshot();
        const cp::Revision revision =
            entity_id == "routing" ? snapshot.routing.revision : snapshot.external_output.revision;
        session().handle_ui_message(
            cp::DiscreteCommand{request_id, entity_id, revision, std::move(body)});
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

    void configure() {
        ASSERT_TRUE(command(kEntity, cp::SetExternalOutputConfig{destination()}));
        ASSERT_TRUE(command(kEntity, cp::SetExternalOutputKey{kKey}));
    }

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
    common::FakeClock clock_;
    common::FakeLogger logger_;
    std::optional<HostSession> session_;
    int request_number_ = 0;
    std::optional<ErrorCode> last_reject_;
};

TEST_F(HostExternalOutputTest, StartsStoppedEvenWithSavedConfigAndKey) {
    HostSettings saved;
    saved.external_output = destination();
    settings_.host() = saved;
    const std::string key = kKey;
    secrets_.put(SecretSlot::kExternalOutputKey, std::vector<std::uint8_t>(key.begin(), key.end()));

    const auto info = output();
    EXPECT_EQ(info.entity_id, kEntity);
    EXPECT_EQ(info.state, ExternalOutputState::kStopped);
    EXPECT_EQ(info.config, destination());
    EXPECT_TRUE(info.key_configured);
    EXPECT_TRUE(external_output_.starts().empty());
}

TEST_F(HostExternalOutputTest, CorruptedSavedKeyIsTreatedAsUnset) {
    secrets_.put(SecretSlot::kExternalOutputKey, {'x', 'y', 'z'});
    EXPECT_FALSE(output().key_configured);
}

TEST_F(HostExternalOutputTest, RefusesToStartWithoutKeyOrDestination) {
    ASSERT_TRUE(command(kEntity, cp::SetExternalOutputConfig{destination()}));
    EXPECT_FALSE(command(kEntity, cp::SetExternalOutputRunning{true}));
    EXPECT_EQ(last_reject_, ErrorCode::kInvalidState);
    EXPECT_FALSE(command(kEntity, cp::TestExternalOutput{}));
    EXPECT_EQ(last_reject_, ErrorCode::kInvalidState);
    EXPECT_TRUE(external_output_.starts().empty());
    EXPECT_TRUE(external_output_.tests().empty());

    // 鍵があっても宛先がなければ送らない。
    ASSERT_TRUE(command(kEntity, cp::SetExternalOutputConfig{cp::ExternalOutputConfig{}}));
    ASSERT_TRUE(command(kEntity, cp::SetExternalOutputKey{kKey}));
    EXPECT_FALSE(command(kEntity, cp::SetExternalOutputRunning{true}));
    EXPECT_TRUE(external_output_.starts().empty());
}

TEST_F(HostExternalOutputTest, StartsWithThePlanBuiltFromTheConfig) {
    configure();
    const auto started = command(kEntity, cp::SetExternalOutputRunning{true});
    ASSERT_TRUE(started);
    EXPECT_EQ(*started, output().revision);
    ASSERT_EQ(external_output_.starts().size(), 1U);
    const auto& plan = external_output_.starts().front();
    EXPECT_EQ(plan.host, "192.0.2.10");
    EXPECT_EQ(plan.port, 5004);
    EXPECT_EQ(plan.source_id, "master");
    EXPECT_EQ(plan.channels, 1U);
    EXPECT_EQ(plan.payload_type, 100);
    EXPECT_EQ(plan.suite, transport::SrtpCryptoSuite::kAesCm128HmacSha1_32);
    EXPECT_EQ(output().state, ExternalOutputState::kRunning);
    EXPECT_EQ(output().last_result, ExternalOutputResult::kOk);

    ASSERT_TRUE(command(kEntity, cp::SetExternalOutputRunning{false}));
    EXPECT_EQ(output().state, ExternalOutputState::kStopped);
    EXPECT_FALSE(external_output_.running());
}

TEST_F(HostExternalOutputTest, FailedStartStaysStoppedAndReportsTheCause) {
    configure();
    external_output_.fail_start(ExternalOutputResult::kResolveFailed);
    ASSERT_TRUE(command(kEntity, cp::SetExternalOutputRunning{true}));
    EXPECT_EQ(output().state, ExternalOutputState::kStopped);
    EXPECT_EQ(output().last_result, ExternalOutputResult::kResolveFailed);
    const auto notice = ui_.last<cp::Notice>();
    ASSERT_TRUE(notice);
    EXPECT_EQ(notice->code, "external_output_failed");
}

TEST_F(HostExternalOutputTest, ConfigAndKeyCanOnlyChangeWhileStopped) {
    configure();
    ASSERT_TRUE(command(kEntity, cp::SetExternalOutputRunning{true}));
    auto changed = destination();
    changed.port = 6000;
    EXPECT_FALSE(command(kEntity, cp::SetExternalOutputConfig{changed}));
    EXPECT_EQ(last_reject_, ErrorCode::kInvalidState);
    EXPECT_FALSE(command(kEntity, cp::SetExternalOutputKey{kKey}));
    EXPECT_EQ(last_reject_, ErrorCode::kInvalidState);
    EXPECT_EQ(output().config.port, 5004);
}

TEST_F(HostExternalOutputTest, ConfigIsSavedAndKeyIsStoredAsSecretButNeverPublished) {
    configure();
    ASSERT_TRUE(settings_.host());
    EXPECT_EQ(settings_.host()->external_output, destination());
    const auto stored = secrets_.stored(SecretSlot::kExternalOutputKey);
    ASSERT_TRUE(stored);
    EXPECT_EQ(std::string(stored->begin(), stored->end()),
              transport::format_srtp_master_key(transport::parse_srtp_master_key(kKey).value()));
    // UI に送ったメッセージには鍵の文字列が現れない。
    const std::string body = std::string(kKey).substr(7);
    for (const auto& event : ui_.all<cp::ExternalOutputUpdated>()) {
        EXPECT_EQ(event.external_output.config.host.find(body), std::string::npos);
    }
    EXPECT_TRUE(output().key_configured);
}

TEST_F(HostExternalOutputTest, RejectsMalformedKeysAndUnknownSources) {
    EXPECT_FALSE(command(kEntity, cp::SetExternalOutputKey{"inline:AAAA"}));
    EXPECT_EQ(last_reject_, ErrorCode::kInvalidState);
    // すべて 0 の鍵（入力欄の初期値のまま）も受け付けない。
    EXPECT_FALSE(
        command(kEntity, cp::SetExternalOutputKey{"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"}));
    EXPECT_FALSE(output().key_configured);

    EXPECT_FALSE(command(kEntity, cp::SetExternalOutputConfig{destination("bus-missing")}));
    EXPECT_EQ(last_reject_, ErrorCode::kNotFound);
}

TEST_F(HostExternalOutputTest, StopsWhenTheSourceBusIsRemovedFromRouting) {
    ASSERT_TRUE(command("routing", cp::StageRouting{bus_routing()}));
    ASSERT_TRUE(command("routing", cp::CommitRouting{false}));
    ASSERT_TRUE(command(kEntity, cp::SetExternalOutputConfig{destination("bus-stream")}));
    ASSERT_TRUE(command(kEntity, cp::SetExternalOutputKey{kKey}));
    ASSERT_TRUE(command(kEntity, cp::SetExternalOutputRunning{true}));
    EXPECT_EQ(external_output_.starts().back().source_id, "bus-stream");

    ASSERT_TRUE(command("routing", cp::StageRouting{cp::RoutingConfig{}}));
    ASSERT_TRUE(command("routing", cp::CommitRouting{false}));
    EXPECT_EQ(output().state, ExternalOutputState::kStopped);
    EXPECT_FALSE(external_output_.running());
    const auto notice = ui_.last<cp::Notice>();
    ASSERT_TRUE(notice);
    EXPECT_EQ(notice->code, "external_output_source_removed");
}

TEST_F(HostExternalOutputTest, ConnectionTestRunsOnlyWhileStoppedAndReportsTheResult) {
    configure();
    ASSERT_TRUE(command(kEntity, cp::TestExternalOutput{}));
    EXPECT_EQ(output().state, ExternalOutputState::kTesting);
    ASSERT_EQ(external_output_.tests().size(), 1U);
    // テスト中は送信を始めない。
    EXPECT_FALSE(command(kEntity, cp::SetExternalOutputRunning{true}));
    EXPECT_FALSE(command(kEntity, cp::TestExternalOutput{}));

    const auto revision = output().revision;
    session().on_external_output_test_finished(ExternalOutputResult::kSendFailed);
    EXPECT_EQ(output().state, ExternalOutputState::kStopped);
    EXPECT_EQ(output().last_result, ExternalOutputResult::kSendFailed);
    // 結果は観測値なので revision は進めない。
    EXPECT_EQ(output().revision, revision);

    // 送信中は接続テストをしない。
    ASSERT_TRUE(command(kEntity, cp::SetExternalOutputRunning{true}));
    EXPECT_FALSE(command(kEntity, cp::TestExternalOutput{}));
    EXPECT_EQ(last_reject_, ErrorCode::kInvalidState);
}

TEST_F(HostExternalOutputTest, LateTestResultAfterStopIsIgnored) {
    configure();
    ASSERT_TRUE(command(kEntity, cp::TestExternalOutput{}));
    ASSERT_TRUE(command(kEntity, cp::SetExternalOutputRunning{false}));
    EXPECT_EQ(external_output_.stops(), 1);
    session().on_external_output_test_finished(ExternalOutputResult::kOk);
    EXPECT_EQ(output().state, ExternalOutputState::kStopped);
    EXPECT_FALSE(output().last_result);
}

TEST_F(HostExternalOutputTest, CommandsTargetOnlyTheExternalOutputEntity) {
    EXPECT_FALSE(command("session", cp::SetExternalOutputRunning{true}));
    EXPECT_FALSE(command("routing", cp::TestExternalOutput{}));
}

}  // namespace
}  // namespace vocroz::session
