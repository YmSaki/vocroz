// SPDX-License-Identifier: AGPL-3.0-only
//
// ClientCapture の実装。

#include "client_capture.h"

#include <cmath>
#include <string_view>

namespace vocroz::client {

ClientCapture::ClientCapture(CaptureProcessor& processor, InputDeviceOpener& opener,
                             common::Logger& logger)
    : processor_(processor), opener_(opener), logger_(logger) {}

ClientCapture::~ClientCapture() {
    if (input_) {
        input_->stop();
    }
}

void ClientCapture::select_input(const std::optional<std::string>& device_id) {
    if (input_) {
        // 止めてから破棄し、古いデバイスのコールバックが processor_ を呼ばないようにする。
        input_->stop();
        input_.reset();
    }
    device_lost_.store(false, std::memory_order_release);
    if (!device_id) {
        return;
    }
    auto opened = opener_.open(*device_id);
    if (!opened) {
        logger_.log(common::LogLevel::kWarning,
                    "client: the selected input device could not be opened");
        return;
    }
    auto input = std::move(opened).value();
    processor_.restart(params_);
    params_pending_ = false;
    if (!input->start(processor_, *this)) {
        logger_.log(common::LogLevel::kWarning,
                    "client: the selected input device could not start");
        return;
    }
    input_ = std::move(input);
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

void ClientCapture::push_params() {
    params_pending_ = true;
    flush_params();
}

void ClientCapture::flush_params() {
    if (!params_pending_) {
        return;
    }
    if (!input_) {
        // 入力なしの間はオーディオスレッドが取り出さない。次にデバイスを開くときに restart() で
        // 最新の値から始める。
        params_pending_ = false;
        return;
    }
    // 溜まりきっていたら、次の周期で渡し直す。
    params_pending_ = !processor_.set_params(params_);
}

void ClientCapture::on_device_lost() noexcept {
    device_lost_.store(true, std::memory_order_release);
}

bool ClientCapture::take_device_lost() noexcept {
    return device_lost_.exchange(false, std::memory_order_acq_rel);
}

}  // namespace vocroz::client
