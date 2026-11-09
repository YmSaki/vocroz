// SPDX-License-Identifier: AGPL-3.0-only
//
// external_stream_worker.h の実装。

#include "external_stream_worker.h"

#include <array>
#include <cmath>
#include <numbers>
#include <span>
#include <utility>

#include "vocroz/transport/constants.h"

namespace vocroz::engine {
namespace {

namespace cp = control_protocol;

// 試験音: 1 kHz、-20 dBFS（振幅 0.1）。受信機のメーターで見分けやすく、放送機器の入力を
// 振り切らない、一般的な基準信号の大きさにする。
constexpr double kTestToneHz = 1000.0;
constexpr float kTestToneAmplitude = 0.1F;

// 1 回に作る試験音の長さ。外部出力のフレーム長（5ms）と同じにし、1 フレームずつ送る。
constexpr std::size_t kTestBlockSamples =
    transport::frame_samples(transport::FrameDuration::k5000Us);
constexpr auto kTestBlockDuration = std::chrono::microseconds{5000};

}  // namespace

ExternalStreamPipe::ExternalStreamPipe(transport::OpusFrameEncoder encoder)
    : encoder_(std::move(encoder), *this) {}

bool ExternalStreamPipe::push_frame(const transport::MediaFrame& frame) noexcept
    VOCROZ_NONBLOCKING {
    const bool pushed = frames_.try_push(frame);
    wakeup_.notify();
    return pushed;
}

ExternalStreamWorker::ExternalStreamWorker(std::shared_ptr<ExternalStreamPipe> pipe,
                                           std::unique_ptr<transport::DatagramSocket> socket,
                                           transport::ExternalStreamSender sender)
    : pipe_(std::move(pipe)), socket_(std::move(socket)), sender_(std::move(sender)) {}

ExternalStreamWorker::~ExternalStreamWorker() {
    stop();
}

void ExternalStreamWorker::start_streaming() {
    if (thread_.joinable()) {
        return;
    }
    thread_ = std::thread([this] { run_streaming(); });
}

void ExternalStreamWorker::start_test(std::chrono::milliseconds duration, TestFinished done) {
    if (thread_.joinable()) {
        return;
    }
    thread_ = std::thread([this, duration, done = std::move(done)] { run_test(duration, done); });
}

void ExternalStreamWorker::stop() {
    if (!thread_.joinable()) {
        return;
    }
    stopping_.store(true, std::memory_order_release);
    pipe_->wakeup().notify();
    thread_.join();
}

void ExternalStreamWorker::drain() noexcept {
    while (const auto frame = pipe_->pop()) {
        // 失敗したパケットは ExternalStreamSender が捨てて数える。平文に戻して送る経路はない。
        (void)sender_.send(*frame);
    }
}

void ExternalStreamWorker::run_streaming() noexcept {
    while (!stopping_.load(std::memory_order_acquire)) {
        // 処理の前に世代を読んでおき、処理中に入ったフレームの合図を取りこぼさない。
        const auto seen = pipe_->wakeup().generation();
        drain();
        if (stopping_.load(std::memory_order_acquire)) {
            return;
        }
        pipe_->wakeup().wait(seen);
    }
}

void ExternalStreamWorker::run_test(std::chrono::milliseconds duration,
                                    const TestFinished& done) noexcept {
    std::array<float, kTestBlockSamples> tone{};
    double phase = 0.0;
    const double step =
        2.0 * std::numbers::pi * kTestToneHz / static_cast<double>(transport::kSampleRate);
    const auto blocks = static_cast<std::size_t>(duration / kTestBlockDuration);
    auto next = std::chrono::steady_clock::now();
    for (std::size_t i = 0; i < blocks && !stopping_.load(std::memory_order_acquire); ++i) {
        for (float& sample : tone) {
            sample = kTestToneAmplitude * static_cast<float>(std::sin(phase));
            phase = std::fmod(phase + step, 2.0 * std::numbers::pi);
        }
        // 試験音は L と R に同じ音を入れる。モノラルのプロファイルでは平均で同じ音になる。
        pipe_->encoder().push_stereo(tone, tone);
        drain();
        // 受信機のジッターバッファを溢れさせないよう、実時間の速さで送る。
        next += kTestBlockDuration;
        std::this_thread::sleep_until(next);
    }
    drain();
    if (stopping_.load(std::memory_order_acquire)) {
        return;
    }
    const bool ok = sender_.dropped_packets() == 0 && sender_.sent_packets() > 0;
    done(ok ? cp::ExternalOutputResult::kOk : cp::ExternalOutputResult::kSendFailed);
}

}  // namespace vocroz::engine
