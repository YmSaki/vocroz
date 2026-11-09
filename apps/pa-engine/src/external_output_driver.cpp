// SPDX-License-Identifier: AGPL-3.0-only
//
// external_output_driver.h の実装。

#include "external_output_driver.h"

#include <utility>

#include "vocroz/transport/constants.h"
#include "vocroz/transport/external_stream_sender.h"
#include "vocroz/transport/opus_frame_encoder.h"

namespace vocroz::engine {
namespace {

namespace cp = control_protocol;

// 外部出力のフレーム長。低遅延の方針に合わせて 5ms
// にする（docs/adr/0040-srtp-external-output.md）。
constexpr auto kFrameDuration = transport::FrameDuration::k5000Us;

// モノラルのプロファイルのビットレート。子機から親機へのモノラルと同じ値で、楽器の音質を保てる。
constexpr std::int32_t kMonoBitrateBps = 128000;

}  // namespace

ExternalOutputDriver::ExternalOutputDriver(EngineMixer& mixer, ExternalSocketOpener& sockets,
                                           transport::RandomSource& random, TestFinished on_test,
                                           std::chrono::milliseconds test_length)
    : mixer_(mixer),
      sockets_(sockets),
      random_(random),
      on_test_(std::move(on_test)),
      test_length_(test_length) {}

ExternalOutputDriver::~ExternalOutputDriver() {
    stop();
}

common::Result<void, cp::ExternalOutputResult> ExternalOutputDriver::assemble(
    const session::ExternalOutputPlan& plan, const transport::SrtpMasterKey& key) {
    auto socket = sockets_.open(plan.host, plan.port);
    if (!socket) {
        return common::make_unexpected(socket.error());
    }
    const auto params =
        transport::make_rtp_stream_params(random_, plan.payload_type, kFrameDuration);
    auto sender = transport::ExternalStreamSender::create(plan.suite, key, params, *socket.value());
    if (!sender) {
        // 鍵とペイロードタイプは session が検証済みなので、ここに来るのは libsrtp の初期化の失敗。
        return common::make_unexpected(cp::ExternalOutputResult::kSendFailed);
    }
    transport::OpusEncoderConfig config;
    config.frame_duration = kFrameDuration;
    config.channels = plan.channels;
    config.bitrate_bps = plan.channels == 2 ? transport::kStereoBitrateBps : kMonoBitrateBps;
    auto encoder = transport::OpusFrameEncoder::create(config);
    if (!encoder) {
        return common::make_unexpected(cp::ExternalOutputResult::kSendFailed);
    }
    auto pipe = std::make_shared<ExternalStreamPipe>(std::move(encoder).value());
    // MediaStreamEncoder は初期ミュートで始まる。外部出力はオペレーターが開始を選んだときにだけ
    // 組み立てるので、ここで解除する（立ち上がりはゲインランプでフェードインする）。
    if (!pipe->encoder().set_params({false, 1, 1.0F})) {
        return common::make_unexpected(cp::ExternalOutputResult::kSendFailed);
    }
    worker_ = std::make_unique<ExternalStreamWorker>(pipe, std::move(socket).value(),
                                                     std::move(sender).value());
    pipe_ = std::move(pipe);
    return {};
}

common::Result<void, cp::ExternalOutputResult> ExternalOutputDriver::start(
    const session::ExternalOutputPlan& plan, const transport::SrtpMasterKey& key) {
    stop();
    if (auto assembled = assemble(plan, key); !assembled) {
        return assembled;
    }
    // 送信スレッドを先に起動してから、オーディオスレッドのタップをつなぐ。
    worker_->start_streaming();
    const std::shared_ptr<audio_core::AudioStreamSink> tap(pipe_, &pipe_->encoder());
    if (!mixer_.attach_external_stream(plan.source_id, tap)) {
        stop();
        return common::make_unexpected(cp::ExternalOutputResult::kSendFailed);
    }
    return {};
}

void ExternalOutputDriver::stop() {
    // タップを先に外す。古いグラフが破棄されるまでオーディオスレッドはパイプに書き続けるが、
    // パイプはグラフも共有しているので、送信スレッドを止めても残る。
    mixer_.detach_external_stream();
    if (worker_) {
        worker_->stop();
        worker_.reset();
    }
    pipe_.reset();
}

common::Result<void, cp::ExternalOutputResult> ExternalOutputDriver::start_test(
    const session::ExternalOutputPlan& plan, const transport::SrtpMasterKey& key) {
    stop();
    if (auto assembled = assemble(plan, key); !assembled) {
        return assembled;
    }
    worker_->start_test(test_length_, on_test_);
    return {};
}

}  // namespace vocroz::engine
