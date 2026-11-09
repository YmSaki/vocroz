// SPDX-License-Identifier: AGPL-3.0-only
//
// ReturnOutputDevices の実装。

#include "return_output_devices.h"

#include <utility>

namespace vocroz::client {
namespace {

namespace ac = audio_core;
namespace cp = control_protocol;

}  // namespace

ReturnOutputDevices::ReturnOutputDevices(ac::AudioDeviceCatalog& catalog,
                                         OutputDeviceOpener& opener, ReturnPlayer& player,
                                         common::Logger& logger) noexcept
    : catalog_(catalog), opener_(opener), player_(player), logger_(logger) {}

ReturnOutputDevices::~ReturnOutputDevices() {
    close_output();
}

std::vector<cp::OutputDevice> ReturnOutputDevices::list_outputs() {
    auto listed = catalog_.list(ac::AudioDeviceDirection::kOutput);
    if (!listed) {
        logger_.log(common::LogLevel::kWarning, "client: output devices could not be listed");
        return {};
    }
    std::vector<cp::OutputDevice> devices;
    devices.reserve(listed.value().size());
    for (auto& device : listed.value()) {
        devices.push_back(cp::OutputDevice{std::move(device.id), std::move(device.name)});
    }
    return devices;
}

common::Result<std::string, cp::ErrorCode> ReturnOutputDevices::open_output(
    const std::string& device_id) {
    // 返送のジッターバッファを 2
    // つのオーディオスレッドから読まないよう、開いている出力は先に止める。
    close_output();
    auto output = opener_.open(device_id);
    if (!output) {
        logger_.log(common::LogLevel::kWarning, "client: the return output could not be opened");
        return common::make_unexpected(cp::ErrorCode::kDeviceUnavailable);
    }
    // 前のデバイスの抜去の通知が残っていても、新しいデバイスでは使わない。
    static_cast<void>(player_.take_device_lost());
    if (const auto started = output.value()->start(player_, player_); !started) {
        logger_.log(common::LogLevel::kWarning, "client: the return output could not be started");
        return common::make_unexpected(cp::ErrorCode::kDeviceUnavailable);
    }
    output_ = std::move(output).value();
    // 表示名は一覧から引く。一覧に見つからなくても開けたのだから、ID を名前の代わりにする。
    for (const auto& device : list_outputs()) {
        if (device.id == device_id) {
            return device.name;
        }
    }
    return device_id;
}

void ReturnOutputDevices::close_output() {
    if (output_) {
        output_->stop();
        output_.reset();
    }
}

}  // namespace vocroz::client
