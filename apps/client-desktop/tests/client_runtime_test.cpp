// SPDX-License-Identifier: AGPL-3.0-only
//
// 子機モードの組み立て（ClientRuntime）のテスト。UI の hello にスナップショットで応えること、
// 入力デバイスの一覧と選択がキャプチャまで届くこと、参加の操作から招待の解決とランデブーの登録まで
// 進むこと、入力のメーターを UI へ送ること、返送の出力デバイスの選択と抜去を、アダプターの
// フェイクで確かめる。

#include "client_runtime.h"

#include <chrono>
#include <map>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "vocroz/control_protocol/json_codec.h"

#include "fakes/fake_audio_device_catalog.h"
#include "fakes/fake_audio_input.h"
#include "fakes/fake_clock.h"
#include "fakes/fake_logger.h"
#include "fakes/fake_message_channel.h"
#include "fakes/fake_network_environment.h"
#include "fakes/fake_output_device_opener.h"
#include "fakes/fake_random_source.h"
#include "fakes/fake_rendezvous_client.h"
#include "fakes/fake_settings_store.h"
#include "fakes/fake_udp_endpoint.h"

namespace vocroz::client {
namespace {

using namespace std::chrono_literals;
namespace cp = control_protocol;

const network::UdpAddress kServer =
    network::UdpAddress::from_ipv4({198, 51, 100, 1}, kRendezvousUdpPort);

class FakeInputs final : public InputDeviceOpener {
public:
    [[nodiscard]] common::Result<std::unique_ptr<audio_core::AudioInput>,
                                 audio_core::AudioDeviceError>
    open(std::string_view device_id) override {
        opened.emplace_back(device_id);
        auto input = std::make_unique<audio_core::FakeAudioInput>();
        last = input.get();
        return std::unique_ptr<audio_core::AudioInput>(std::move(input));
    }
    std::vector<std::string> opened;
    audio_core::FakeAudioInput* last = nullptr;
};

class ClientRuntimeTest : public ::testing::Test {
protected:
    ClientRuntimeTest() {
        devices_.set_devices(audio_core::AudioDeviceDirection::kInput,
                             {audio_core::AudioDeviceInfo{.id = "mic-1", .name = "Mic 1"}});
        devices_.set_devices(audio_core::AudioDeviceDirection::kOutput,
                             {audio_core::AudioDeviceInfo{.id = "spk-1", .name = "Headphones"}});
        outputs_.available = {"spk-1"};
        environment_.resolved = {kServer};
        settings_.client() = session::ClientSettings{"Alice", "mic-1", 0.0};
        runtime_ =
            ClientRuntime::create(ClientRuntime::Ports{.ui_channel = channel_,
                                                       .endpoint = endpoint_,
                                                       .router = router_,
                                                       .inbox = inbox_,
                                                       .path = path_,
                                                       .returns = *returns_,
                                                       .environment = environment_,
                                                       .devices = devices_,
                                                       .inputs = inputs_,
                                                       .app_audio = app_audio_,
                                                       .outputs = outputs_,
                                                       .rendezvous = rendezvous_,
                                                       .invites = invites_,
                                                       .settings = settings_,
                                                       .random = random_,
                                                       .clock = clock_,
                                                       .logger = logger_},
                                  ClientRuntime::Config{.rendezvous_host = "rendezvous.example",
                                                        .rendezvous_port = kRendezvousUdpPort,
                                                        .encoder = {}})
                .value();
        runtime_->start();
    }

    // UI に送ったメッセージをデコードして返し、記録を進める。
    std::vector<cp::EngineToUiMessage> take_ui_messages() {
        std::vector<cp::EngineToUiMessage> out;
        const auto sent = channel_.sent();
        for (; read_ < sent.size(); ++read_) {
            out.push_back(cp::decode_engine_to_ui(sent[read_]).value());
        }
        return out;
    }

    std::optional<cp::ClientSnapshot> latest_snapshot() {
        std::optional<cp::ClientSnapshot> snapshot;
        for (const auto& m : take_ui_messages()) {
            if (const auto* s = std::get_if<cp::ClientSnapshotEvent>(&m)) {
                snapshot = s->snapshot;
            } else if (const auto* u = std::get_if<cp::ClientUpdated>(&m)) {
                snapshot = u->client;
            }
        }
        return snapshot;
    }

    void send_ui(const cp::UiToEngineMessage& message) {
        channel_.deliver(cp::encode_ui_to_engine(message));
    }

    cp::FakeMessageChannel channel_;
    network::FakeUdpEndpoint endpoint_;
    PeerInbox inbox_;
    std::unique_ptr<transport::PlayoutStream> returns_ =
        transport::PlayoutStream::create(transport::PlayoutConfig{}).value();
    HostReceivePath path_{inbox_, *returns_};
    network::ReceiveRouter router_{path_};
    FakeNetworkEnvironment environment_;
    audio_core::FakeAudioDeviceCatalog devices_;
    FakeInputs inputs_;
    FakeInputs app_audio_;
    FakeOutputDeviceOpener outputs_;
    session::FakeRendezvousClient rendezvous_;
    InviteInbox invites_;
    session::FakeSettingsStore settings_;
    transport::FakeRandomSource random_;
    common::FakeClock clock_;
    common::FakeLogger logger_;
    std::unique_ptr<ClientRuntime> runtime_;
    std::size_t read_ = 0;
};

TEST_F(ClientRuntimeTest, AnswersHelloWithMutedSnapshot) {
    send_ui(cp::Hello{});
    const auto snapshot = latest_snapshot();
    ASSERT_TRUE(snapshot);
    EXPECT_EQ(snapshot->client_state, cp::ClientState::kDisconnected);
    EXPECT_TRUE(snapshot->send_muted);
    ASSERT_EQ(snapshot->input_devices.size(), 1U);
    EXPECT_EQ(snapshot->input_devices[0].id, "mic-1");
}

TEST_F(ClientRuntimeTest, OpensSavedInputDeviceOnStart) {
    EXPECT_EQ(inputs_.opened, std::vector<std::string>{"mic-1"});
    EXPECT_FALSE(runtime_->in_session());
}

TEST_F(ClientRuntimeTest, JoinResolvesInviteAndStartsRendezvous) {
    send_ui(cp::Hello{});
    const auto snapshot = latest_snapshot().value();
    send_ui(cp::DiscreteCommand{.request_id = "r1",
                                .entity_id = snapshot.entity_id,
                                .expected_revision = snapshot.revision,
                                .body = cp::Join{.invite = "ABCDE-12345"}});
    ASSERT_EQ(rendezvous_.resolutions().size(), 1U);
    EXPECT_TRUE(runtime_->in_session());

    session::ResolvedInvite resolved;
    resolved.endpoint.host_public_key[0] = 1;
    ASSERT_TRUE(invites_.push(rendezvous_.resolutions()[0], resolved));
    clock_.advance(10ms);
    runtime_->poll();  // 解決の結果を session へ渡し、HostLink::connect() が呼ばれる
    runtime_->poll();  // 接続の要求を反映して、ランデブーサーバーへ登録する

    EXPECT_GE(endpoint_.count_to(kServer), 1U);
}

TEST_F(ClientRuntimeTest, PublishesInputMeterPeriodically) {
    std::vector<float> block(128, 0.5F);
    inputs_.last->capture(block);
    clock_.advance(kMeterInterval);
    runtime_->poll();

    bool found = false;
    for (const auto& m : take_ui_messages()) {
        if (const auto* meters = std::get_if<cp::Meters>(&m)) {
            ASSERT_EQ(meters->levels.size(), 1U);
            ASSERT_TRUE(meters->levels[0].peak_db);
            EXPECT_NEAR(*meters->levels[0].peak_db, -6.02, 0.01);
            found = true;
        }
    }
    EXPECT_TRUE(found);
}

TEST_F(ClientRuntimeTest, RefreshesDevicesWhenInputIsUnplugged) {
    devices_.set_devices(audio_core::AudioDeviceDirection::kInput, {});
    inputs_.last->simulate_device_lost();
    runtime_->poll();

    send_ui(cp::Hello{});
    const auto snapshot = latest_snapshot().value();
    EXPECT_TRUE(snapshot.input_devices.empty());
    EXPECT_TRUE(snapshot.input_missing);
}

TEST_F(ClientRuntimeTest, OpensTheSelectedReturnOutputAndReportsItsLoss) {
    send_ui(cp::Hello{});
    auto snapshot = latest_snapshot().value();
    ASSERT_EQ(snapshot.output_devices.size(), 1U);
    send_ui(cp::DiscreteCommand{.request_id = "r1",
                                .entity_id = snapshot.entity_id,
                                .expected_revision = snapshot.revision,
                                .body = cp::SelectOutput{.device_id = "spk-1"}});
    ASSERT_NE(outputs_.last, nullptr);
    EXPECT_TRUE(outputs_.last->is_started());
    EXPECT_EQ(settings_.client()->output_device_id, "spk-1");

    devices_.set_devices(audio_core::AudioDeviceDirection::kOutput, {});
    outputs_.last->simulate_device_lost();
    runtime_->poll();
    snapshot = latest_snapshot().value();
    EXPECT_EQ(snapshot.selected_output_id, "spk-1");
    EXPECT_TRUE(snapshot.output_missing);
    EXPECT_TRUE(snapshot.output_devices.empty());
}

}  // namespace
}  // namespace vocroz::client
