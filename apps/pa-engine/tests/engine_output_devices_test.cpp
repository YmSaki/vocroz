// SPDX-License-Identifier: AGPL-3.0-only
//
// EngineOutputDevices の単体テスト。一覧の変換、指定したデバイスだけを開くこと、
// 開き直す前に前の出力を止めること、開けないときに何も開かないことを確かめる。

#include "engine_output_devices.h"

#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "fakes/fake_audio_device_catalog.h"
#include "fakes/fake_audio_output.h"
#include "fakes/fake_logger.h"

namespace vocroz::engine {
namespace {

namespace ac = audio_core;
namespace cp = control_protocol;

class SilentRender final : public ac::AudioRenderCallback {
public:
    void render(ac::InterleavedBuffer out) noexcept VOCROZ_NONBLOCKING override {
        for (auto& sample : out.samples) {
            sample = 0.0F;
        }
    }
};

class CountingListener final : public ac::AudioDeviceListener {
public:
    void on_device_lost() noexcept override {
        ++lost;
    }
    int lost = 0;
};

// 開始と停止の順序を記録する出力。破棄されたあとも記録を読めるよう、記録はテストが持つ。
class RecordingOutput final : public ac::AudioOutput {
public:
    RecordingOutput(std::string id, std::vector<std::string>& events)
        : id_(std::move(id)), events_(events) {}

    common::Result<void, ac::AudioDeviceError> start(ac::AudioRenderCallback& callback,
                                                     ac::AudioDeviceListener& listener) override {
        auto started = inner_.start(callback, listener);
        if (started) {
            events_.push_back("start " + id_);
        }
        return started;
    }

    void stop() noexcept override {
        if (inner_.is_started()) {
            events_.push_back("stop " + id_);
        }
        inner_.stop();
    }

    [[nodiscard]] std::size_t channel_count() const noexcept override {
        return inner_.channel_count();
    }

    ac::FakeAudioOutput& inner() noexcept {
        return inner_;
    }

private:
    std::string id_;
    std::vector<std::string>& events_;
    ac::FakeAudioOutput inner_;
};

class EngineOutputDevicesTest : public ::testing::Test {
protected:
    EngineOutputDevicesTest() {
        catalog_.set_devices(ac::AudioDeviceDirection::kOutput,
                             {ac::AudioDeviceInfo{"dev-1", "スピーカー", 2, true},
                              ac::AudioDeviceInfo{"dev-2", "USB Audio", 2, false}});
    }

    EngineOutputDevices::OutputFactory factory() {
        return [this](std::string_view id)
                   -> common::Result<std::unique_ptr<ac::AudioOutput>, ac::AudioDeviceError> {
            ++created_[std::string(id)];
            if (id == "dev-broken") {
                return common::make_unexpected(ac::AudioDeviceError::kDeviceNotFound);
            }
            auto output = std::make_unique<RecordingOutput>(std::string(id), events_);
            last_ = &output->inner();
            return std::unique_ptr<ac::AudioOutput>(std::move(output));
        };
    }

    ac::FakeAudioDeviceCatalog catalog_;
    SilentRender render_;
    CountingListener listener_;
    common::FakeLogger logger_;
    std::map<std::string, int> created_;
    std::vector<std::string> events_;
    ac::FakeAudioOutput* last_ = nullptr;
    EngineOutputDevices devices_{catalog_, factory(), render_, listener_, logger_};
};

TEST_F(EngineOutputDevicesTest, ListsOutputDevicesFromTheCatalog) {
    const auto listed = devices_.list_outputs();
    ASSERT_EQ(listed.size(), 2U);
    EXPECT_EQ(listed[0], (cp::OutputDevice{"dev-1", "スピーカー"}));
    EXPECT_EQ(listed[1], (cp::OutputDevice{"dev-2", "USB Audio"}));
}

TEST_F(EngineOutputDevicesTest, ListsNothingWhenTheCatalogFails) {
    catalog_.set_error(ac::AudioDeviceError::kInternal);
    EXPECT_TRUE(devices_.list_outputs().empty());
}

TEST_F(EngineOutputDevicesTest, OpensOnlyTheRequestedDeviceAndReturnsItsName) {
    const auto opened = devices_.open_output("dev-2");
    ASSERT_TRUE(opened);
    EXPECT_EQ(opened.value(), "USB Audio");
    EXPECT_EQ(created_, (std::map<std::string, int>{{"dev-2", 1}}));
    ASSERT_NE(last_, nullptr);
    EXPECT_TRUE(last_->is_started());
    // 抜去は渡したリスナーに届く。
    last_->simulate_device_lost();
    EXPECT_EQ(listener_.lost, 1);
}

TEST_F(EngineOutputDevicesTest, StopsThePreviousOutputBeforeOpeningAnother) {
    ASSERT_TRUE(devices_.open_output("dev-1"));
    ASSERT_TRUE(devices_.open_output("dev-2"));
    // ミキサーを 2 つのオーディオスレッドから呼ばないよう、止めてから次を始める。
    EXPECT_EQ(events_, (std::vector<std::string>{"start dev-1", "stop dev-1", "start dev-2"}));
}

TEST_F(EngineOutputDevicesTest, OpensNothingWhenTheDeviceCannotBeOpened) {
    ASSERT_TRUE(devices_.open_output("dev-1"));
    const auto opened = devices_.open_output("dev-broken");
    ASSERT_FALSE(opened);
    EXPECT_EQ(opened.error(), cp::ErrorCode::kDeviceUnavailable);
    // 前のデバイスは止まったまま。前のデバイスにも、ほかのデバイスにも戻らない。
    EXPECT_EQ(events_, (std::vector<std::string>{"start dev-1", "stop dev-1"}));
    EXPECT_EQ(created_, (std::map<std::string, int>{{"dev-1", 1}, {"dev-broken", 1}}));
}

TEST_F(EngineOutputDevicesTest, UsesTheIdAsTheNameWhenTheDeviceIsNotListed) {
    const auto opened = devices_.open_output("dev-unlisted");
    ASSERT_TRUE(opened);
    EXPECT_EQ(opened.value(), "dev-unlisted");
}

TEST_F(EngineOutputDevicesTest, CloseStopsTheOutput) {
    ASSERT_TRUE(devices_.open_output("dev-1"));
    devices_.close_output();
    EXPECT_FALSE(last_->is_started());
    // 開いていなくても閉じてよい。
    devices_.close_output();
}

}  // namespace
}  // namespace vocroz::engine
