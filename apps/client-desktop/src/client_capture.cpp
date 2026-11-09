// SPDX-License-Identifier: AGPL-3.0-only
//
// ClientCapture の実装。

#include "client_capture.h"

#include <algorithm>
#include <cmath>
#include <string_view>
#include <utility>

namespace vocroz::client {
namespace {

namespace cp = control_protocol;

// 入力の種類ごとの経路。
std::size_t lane_of(cp::InputSourceKind kind) noexcept {
    switch (kind) {
        case cp::InputSourceKind::kMicrophone:
            return kMicrophoneLane;
        case cp::InputSourceKind::kApplicationAudio:
            return kAppAudioLane;
    }
    return kMicrophoneLane;
}

}  // namespace

ClientCapture::ClientCapture(Ports ports, const transport::OpusEncoderConfig& encoder,
                             transport::OpusFrameEncoder mic)
    : ports_(ports), encoder_config_(encoder) {
    Lane& lane = lanes_[kMicrophoneLane];
    lane.stream = session::InputStream{};
    lane.processor = std::make_unique<CaptureProcessor>(std::move(mic), ports_.sender,
                                                        kMicrophoneLane, lane.stream->stream_id);
}

ClientCapture::~ClientCapture() {
    for (Lane& lane : lanes_) {
        stop(lane);
    }
}

void ClientCapture::select_input(const std::optional<std::string>& device_id) {
    mic_device_ = device_id;
    Lane& lane = lanes_[kMicrophoneLane];
    stop(lane);
    lane.device_lost.store(false, std::memory_order_release);
    open(kMicrophoneLane);
}

void ClientCapture::set_send_muted(bool muted, std::uint32_t epoch) {
    params_.muted = muted;
    params_.mute_epoch = epoch;
    push_params();
}

void ClientCapture::set_send_level(double level_db) {
    params_.gain = static_cast<float>(std::pow(10.0, level_db / 20.0));
    push_params();
}

void ClientCapture::set_streams(const std::vector<session::InputStream>& streams) {
    for (std::size_t index = 0; index < lanes_.size(); ++index) {
        std::optional<session::InputStream> wanted;
        const auto it = std::ranges::find_if(
            streams, [&](const session::InputStream& s) { return lane_of(s.kind) == index; });
        if (it != streams.end()) {
            wanted = *it;
        }
        if (wanted != lanes_[index].stream) {
            reconfigure(index, wanted);
        }
    }
}

void ClientCapture::reconfigure(std::size_t lane_index,
                                const std::optional<session::InputStream>& stream) {
    Lane& lane = lanes_[lane_index];
    // 止めてから破棄し、古いデバイスのコールバックが古い processor を呼ばないようにする。
    stop(lane);
    lane.processor.reset();
    lane.stream = stream;
    if (!stream) {
        return;
    }
    transport::OpusEncoderConfig config = encoder_config_;
    config.channels = stream->channels;
    if (stream->channels > 1) {
        config.bitrate_bps = std::max(config.bitrate_bps, transport::kStereoBitrateBps);
    }
    auto encoder = transport::OpusFrameEncoder::create(config);
    if (!encoder) {
        ports_.logger.log(common::LogLevel::kWarning,
                          "client: the encoder for an input could not be created");
        return;
    }
    lane.processor = std::make_unique<CaptureProcessor>(std::move(encoder).value(), ports_.sender,
                                                        lane_index, stream->stream_id);
    open(lane_index);
}

void ClientCapture::open(std::size_t lane_index) {
    Lane& lane = lanes_[lane_index];
    if (!lane.processor) {
        return;
    }
    const bool mic = lane_index == kMicrophoneLane;
    if (mic && !mic_device_) {
        return;  // 入力なし
    }
    InputDeviceOpener& opener = mic ? ports_.microphones : ports_.app_audio;
    auto opened = opener.open(mic ? std::string_view(*mic_device_) : std::string_view());
    if (!opened) {
        ports_.logger.log(common::LogLevel::kWarning,
                          mic ? "client: the selected input device could not be opened"
                              : "client: the application audio could not be opened");
        return;
    }
    auto input = std::move(opened).value();
    lane.processor->restart(params_);
    lane.params_pending = false;
    if (!input->start(*lane.processor, lane)) {
        ports_.logger.log(common::LogLevel::kWarning,
                          mic ? "client: the selected input device could not start"
                              : "client: the application audio could not start");
        return;
    }
    lane.input = std::move(input);
}

void ClientCapture::stop(Lane& lane) noexcept {
    if (lane.input) {
        lane.input->stop();
        lane.input.reset();
    }
}

void ClientCapture::push_params() {
    for (Lane& lane : lanes_) {
        lane.params_pending = true;
    }
    flush_params();
}

void ClientCapture::flush_params() {
    Lane& app = lanes_[kAppAudioLane];
    if (app.device_lost.exchange(false, std::memory_order_acq_rel)) {
        stop(app);
        open(kAppAudioLane);
    }
    for (Lane& lane : lanes_) {
        if (!lane.params_pending) {
            continue;
        }
        if (!lane.input) {
            // 入力なしの間はオーディオスレッドが取り出さない。次にデバイスを開くときに restart() で
            // 最新の値から始める。
            lane.params_pending = false;
            continue;
        }
        // 溜まりきっていたら、次の周期で渡し直す。
        lane.params_pending = !lane.processor->set_params(params_);
    }
}

bool ClientCapture::take_device_lost() noexcept {
    return lanes_[kMicrophoneLane].device_lost.exchange(false, std::memory_order_acq_rel);
}

float ClientCapture::take_input_peak() noexcept {
    auto& processor = lanes_[kMicrophoneLane].processor;
    return processor ? processor->take_input_peak() : 0.0F;
}

}  // namespace vocroz::client
