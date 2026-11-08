// SPDX-License-Identifier: AGPL-3.0-only
//
// tests/data/ の全メッセージの例を使った往復のテスト。
// 例をデコードしてエンコードし直すと元と同じ JSON になること、
// 例がすべての種別と、状態とエラーコードのすべての正規名を含むことを確かめる。
// UI のテストも同じ例を読むので、C++ と TypeScript の型の食い違いがここと UI のテストで見つかる。

#include <cstddef>
#include <fstream>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <type_traits>
#include <typeinfo>
#include <variant>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "vocroz/control_protocol/json_codec.h"

namespace vocroz::control_protocol {
namespace {

nlohmann::json load_examples(const std::string& file_name) {
    std::ifstream file{std::string{VOCROZ_CONTROL_PROTOCOL_EXAMPLES_DIR} + "/" + file_name};
    EXPECT_TRUE(file.is_open()) << file_name;
    std::stringstream buffer;
    buffer << file.rdbuf();
    auto examples = nlohmann::json::parse(buffer.str(), nullptr, false);
    EXPECT_TRUE(examples.is_array()) << file_name;
    return examples;
}

/// デコードした例に現れた列挙子を数える。
class Coverage {
public:
    template <typename E>
    void see(E value) {
        seen_[typeid(E).name()].insert(static_cast<int>(value));
    }

    template <typename E>
    void see(const std::optional<E>& value) {
        if (value) {
            see(*value);
        }
    }

    /// 現れた列挙子の数を返す。
    template <typename E>
    [[nodiscard]] std::size_t count() const {
        const auto it = seen_.find(typeid(E).name());
        return it == seen_.end() ? 0 : it->second.size();
    }

    /// 0 から last までの列挙子がすべて現れたかを返す。
    template <typename E>
    [[nodiscard]] bool covers_all_up_to(E last) const {
        return count<E>() == static_cast<std::size_t>(last) + 1;
    }

    void see(const SessionInfo& session) {
        see(session.state);
        see(session.error_code);
        see(session.output_device_state);
    }

    void see(const ChannelStrip& channel) {
        see(channel.connection_state);
        see(channel.signal_state);
        see(channel.error_code);
    }

    void see(const MasterStrip& master) {
        see(master.master_mute_reason);
    }

    void see(const ClientSnapshot& client) {
        see(client.client_state);
        see(client.failure_code);
    }

private:
    std::map<std::string, std::set<int>> seen_;
};

TEST(JsonCodecExamplesTest, UiToEngineExamplesRoundTripAndCoverEveryType) {
    std::set<std::size_t> message_types;
    std::set<std::size_t> command_types;
    for (const auto& example : load_examples("ui_to_engine.json")) {
        const auto decoded = decode_ui_to_engine(example.dump());
        ASSERT_TRUE(decoded) << example.dump() << " field=" << decoded.error().field;
        EXPECT_EQ(nlohmann::json::parse(encode_ui_to_engine(decoded.value())), example);

        message_types.insert(decoded.value().index());
        if (const auto* command = std::get_if<DiscreteCommand>(&decoded.value())) {
            command_types.insert(command->body.index());
        }
    }
    EXPECT_EQ(message_types.size(), std::variant_size_v<UiToEngineMessage>);
    EXPECT_EQ(command_types.size(), std::variant_size_v<DiscreteCommandBody>);
}

TEST(JsonCodecExamplesTest, EngineToUiExamplesRoundTripAndCoverEveryTypeAndName) {
    std::set<std::size_t> message_types;
    Coverage coverage;
    for (const auto& example : load_examples("engine_to_ui.json")) {
        const auto decoded = decode_engine_to_ui(example.dump());
        ASSERT_TRUE(decoded) << example.dump() << " field=" << decoded.error().field;
        EXPECT_EQ(nlohmann::json::parse(encode_engine_to_ui(decoded.value())), example);

        message_types.insert(decoded.value().index());
        std::visit(
            [&coverage](const auto& event) {
                using T = std::decay_t<decltype(event)>;
                if constexpr (std::is_same_v<T, MixerSnapshotEvent>) {
                    coverage.see(event.snapshot.session);
                    for (const auto& channel : event.snapshot.channels) {
                        coverage.see(channel);
                    }
                    coverage.see(event.snapshot.master);
                } else if constexpr (std::is_same_v<T, ClientSnapshotEvent>) {
                    coverage.see(event.snapshot);
                } else if constexpr (std::is_same_v<T, SessionUpdated>) {
                    coverage.see(event.session);
                } else if constexpr (std::is_same_v<T, ChannelUpdated>) {
                    coverage.see(event.channel);
                } else if constexpr (std::is_same_v<T, MasterUpdated>) {
                    coverage.see(event.master);
                } else if constexpr (std::is_same_v<T, ClientUpdated>) {
                    coverage.see(event.client);
                } else if constexpr (std::is_same_v<T, Reject>) {
                    coverage.see(event.error_code);
                } else if constexpr (std::is_same_v<T, Notice>) {
                    coverage.see(event.severity);
                }
            },
            decoded.value());
    }
    EXPECT_EQ(message_types.size(), std::variant_size_v<EngineToUiMessage>);
    EXPECT_TRUE(coverage.covers_all_up_to(SessionState::kError));
    // PENDING はストリップの状態にならないので、PENDING 以外のすべて。
    EXPECT_EQ(coverage.count<ConnectionState>(),
              static_cast<std::size_t>(ConnectionState::kFailed));
    EXPECT_TRUE(coverage.covers_all_up_to(SignalState::kNoSignal));
    EXPECT_TRUE(coverage.covers_all_up_to(OutputDeviceState::kLost));
    EXPECT_TRUE(coverage.covers_all_up_to(MasterMuteReason::kSafety));
    EXPECT_TRUE(coverage.covers_all_up_to(ClientState::kFailed));
    EXPECT_TRUE(coverage.covers_all_up_to(ErrorCode::kEngineError));
    EXPECT_TRUE(coverage.covers_all_up_to(NoticeSeverity::kWarning));
}

}  // namespace
}  // namespace vocroz::control_protocol
