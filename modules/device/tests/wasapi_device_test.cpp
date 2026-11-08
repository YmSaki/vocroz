// SPDX-License-Identifier: AGPL-3.0-only
//
// WASAPI のアダプター（factories.h）の単体テスト。Windows でだけビルドする。
// デバイスがない環境（CI のランナー）では、デバイスが要るテストをスキップする。

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <numbers>
#include <string>
#include <thread>

#include <gtest/gtest.h>

#include "vocroz/device/factories.h"

#include "fakes/fake_logger.h"

namespace vocroz::device {
namespace {

using audio_core::AudioDeviceDirection;
using audio_core::AudioDeviceError;

class CountingRender final : public audio_core::AudioRenderCallback {
public:
    void render(audio_core::InterleavedBuffer out) noexcept VOCROZ_NONBLOCKING override {
        // 実機のスピーカーから音を出さないよう、無音を書く。
        for (float& sample : out.samples) {
            sample = 0.0F;
        }
        frames.fetch_add(out.frames(), std::memory_order_relaxed);
    }
    std::atomic<std::size_t> frames{0};
};

class CountingCapture final : public audio_core::AudioCaptureCallback {
public:
    void on_captured(audio_core::ConstInterleavedBuffer in) noexcept VOCROZ_NONBLOCKING override {
        frames.fetch_add(in.frames(), std::memory_order_relaxed);
    }
    std::atomic<std::size_t> frames{0};
};

class CountingListener final : public audio_core::AudioDeviceListener {
public:
    void on_device_lost() noexcept override {
        lost.fetch_add(1, std::memory_order_relaxed);
    }
    std::atomic<int> lost{0};
};

// 最初に見つかったデバイスの ID を返す。なければ空文字列。
std::string first_device_id(AudioDeviceDirection direction, common::Logger& logger) {
    const auto devices = make_wasapi_audio_device_catalog(logger)->list(direction);
    if (!devices || devices.value().empty()) {
        return {};
    }
    return devices.value().front().id;
}

// ストリームが数周期ぶん動くのを待つ時間。デバイスの周期（3〜10ms）より十分長くする。
constexpr auto kRunTime = std::chrono::milliseconds(300);

TEST(WasapiDeviceTest, CatalogListsBothDirectionsWithoutError) {
    common::FakeLogger logger;
    const auto catalog = make_wasapi_audio_device_catalog(logger);
    for (const auto direction : {AudioDeviceDirection::kOutput, AudioDeviceDirection::kInput}) {
        const auto devices = catalog->list(direction);
        ASSERT_TRUE(devices.has_value());
        for (const auto& device : devices.value()) {
            EXPECT_FALSE(device.id.empty());
        }
    }
}

TEST(WasapiDeviceTest, UnknownEndpointIdIsNotFoundAndNeverFallsBackToDefault) {
    common::FakeLogger logger;
    EXPECT_EQ(make_wasapi_audio_output("{0.0.0.00000000}.{no-such-device}", logger).error(),
              AudioDeviceError::kDeviceNotFound);
    EXPECT_EQ(make_wasapi_audio_input("{0.0.1.00000000}.{no-such-device}", logger).error(),
              AudioDeviceError::kDeviceNotFound);
    // 空の ID を「既定のデバイス」と解釈しない。
    EXPECT_EQ(make_wasapi_audio_output("", logger).error(), AudioDeviceError::kDeviceNotFound);
}

TEST(WasapiDeviceTest, InputEndpointCannotBeOpenedAsOutput) {
    common::FakeLogger logger;
    const std::string input_id = first_device_id(AudioDeviceDirection::kInput, logger);
    if (input_id.empty()) {
        GTEST_SKIP() << "入力デバイスがない";
    }
    EXPECT_EQ(make_wasapi_audio_output(input_id, logger).error(),
              AudioDeviceError::kDeviceNotFound);
}

TEST(WasapiDeviceTest, OutputRendersUntilStoppedAndRejectsDoubleStart) {
    common::FakeLogger logger;
    const std::string output_id = first_device_id(AudioDeviceDirection::kOutput, logger);
    if (output_id.empty()) {
        GTEST_SKIP() << "出力デバイスがない";
    }
    auto output = make_wasapi_audio_output(output_id, logger);
    ASSERT_TRUE(output.has_value());
    EXPECT_GT(output.value()->channel_count(), 0U);
    CountingRender render;
    CountingListener listener;
    ASSERT_TRUE(output.value()->start(render, listener).has_value());
    EXPECT_EQ(output.value()->start(render, listener).error(), AudioDeviceError::kAlreadyStarted);
    std::this_thread::sleep_for(kRunTime);
    output.value()->stop();
    const std::size_t rendered = render.frames.load();
    EXPECT_GT(rendered, 0U);
    // stop() が戻った後はコールバックが呼ばれない。
    std::this_thread::sleep_for(kRunTime);
    EXPECT_EQ(render.frames.load(), rendered);
    EXPECT_EQ(listener.lost.load(), 0);
}

TEST(WasapiDeviceTest, InputCapturesUntilStopped) {
    common::FakeLogger logger;
    const std::string input_id = first_device_id(AudioDeviceDirection::kInput, logger);
    if (input_id.empty()) {
        GTEST_SKIP() << "入力デバイスがない";
    }
    auto input = make_wasapi_audio_input(input_id, logger);
    ASSERT_TRUE(input.has_value());
    CountingCapture capture;
    CountingListener listener;
    ASSERT_TRUE(input.value()->start(capture, listener).has_value());
    std::this_thread::sleep_for(kRunTime);
    input.value()->stop();
    EXPECT_GT(capture.frames.load(), 0U);
    EXPECT_EQ(listener.lost.load(), 0);
}

// ---- 実機での手動確認（docs/testing/alpha-acceptance.md の AT-A07）----
// DISABLED_ のテストは CI では実行されず、--gtest_also_run_disabled_tests を付けたときだけ動く。

// 環境変数の値を返す。未設定なら空文字列。std::getenv は MSVC で非推奨の警告になるので _dupenv_s
// を使う。
std::string read_environment(const char* name) {
    char* value = nullptr;
    std::size_t length = 0;
    if (_dupenv_s(&value, &length, name) != 0 || value == nullptr) {
        return {};
    }
    std::string result(value);
    std::free(value);
    return result;
}

// 実機で聞き取れる小さな音量（-20 dBFS）の 440 Hz の正弦波を全チャンネルに書く。
class SineRender final : public audio_core::AudioRenderCallback {
public:
    void render(audio_core::InterleavedBuffer out) noexcept VOCROZ_NONBLOCKING override {
        constexpr float kAmplitude = 0.1F;
        constexpr double kStep = 2.0 * std::numbers::pi * 440.0 / 48'000.0;
        for (std::size_t frame = 0; frame < out.frames(); ++frame) {
            const auto sample = static_cast<float>(kAmplitude * std::sin(phase_));
            phase_ = std::fmod(phase_ + kStep, 2.0 * std::numbers::pi);
            for (std::size_t channel = 0; channel < out.channels; ++channel) {
                out.samples[frame * out.channels + channel] = sample;
            }
        }
        frames.fetch_add(out.frames(), std::memory_order_relaxed);
    }
    std::atomic<std::size_t> frames{0};

private:
    double phase_ = 0.0;
};

// 出力と入力のデバイスの ID と表示名を一覧にする。手動確認で開くデバイスの ID を調べるために使う。
TEST(WasapiDeviceManualTest, DISABLED_PrintDevices) {
    common::FakeLogger logger;
    const auto catalog = make_wasapi_audio_device_catalog(logger);
    for (const auto direction : {AudioDeviceDirection::kOutput, AudioDeviceDirection::kInput}) {
        std::printf("[%s]\n", direction == AudioDeviceDirection::kOutput ? "output" : "input");
        const auto devices = catalog->list(direction);
        ASSERT_TRUE(devices.has_value());
        for (const auto& device : devices.value()) {
            std::printf("  %s%s (%zu ch)\n    id: %s\n", device.is_system_default ? "* " : "",
                        device.name.c_str(), device.channel_count, device.id.c_str());
        }
    }
}

// 環境変数 VOCROZ_TEST_OUTPUT_ID のデバイスから正弦波を出し、60
// 秒以内に抜去か無効化されるのを待つ。 抜去の通知が 1
// 回届くこと、通知の後はコールバックが呼ばれないことを確かめる。
// 既定のデバイスから音が出ないことは、確認者が耳と録音で確かめる。
TEST(WasapiDeviceManualTest, DISABLED_UnplugOutputReportsLostWithoutFallback) {
    const std::string output_id = read_environment("VOCROZ_TEST_OUTPUT_ID");
    ASSERT_FALSE(output_id.empty()) << "VOCROZ_TEST_OUTPUT_ID に出力デバイスの ID を設定する";
    common::FakeLogger logger;
    auto output = make_wasapi_audio_output(output_id, logger);
    ASSERT_TRUE(output.has_value());
    SineRender render;
    CountingListener listener;
    ASSERT_TRUE(output.value()->start(render, listener).has_value());
    std::printf(
        "440 Hz を出しています。60 "
        "秒以内にデバイスを抜くか、サウンドの設定で無効にしてください。\n");
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
    while (listener.lost.load() == 0 && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    ASSERT_EQ(listener.lost.load(), 1) << "60 秒以内に抜去が通知されなかった";
    const std::size_t rendered = render.frames.load();
    std::printf(
        "抜去を検知しました。30 秒間、どのデバイスからも音が出ないことを確かめてください。\n");
    std::this_thread::sleep_for(std::chrono::seconds(30));
    EXPECT_EQ(listener.lost.load(), 1);
    EXPECT_EQ(render.frames.load(), rendered);  // 通知の後はコールバックが呼ばれない
    output.value()->stop();
    for (const auto& entry : logger.entries()) {
        std::printf("log: %s\n", entry.message.c_str());
    }
}

}  // namespace
}  // namespace vocroz::device
