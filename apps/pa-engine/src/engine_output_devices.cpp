// SPDX-License-Identifier: AGPL-3.0-only
//
// 親機の出力デバイスの一覧、開く、閉じるの実装。

#include "engine_output_devices.h"

#include <utility>

namespace vocroz::engine {
namespace {

namespace ac = audio_core;
namespace cp = control_protocol;

}  // namespace

EngineOutputDevices::EngineOutputDevices(ac::AudioDeviceCatalog& catalog, OutputFactory factory,
                                         ac::AudioRenderCallback& render,
                                         ac::AudioDeviceListener& listener, common::Logger& logger)
    : catalog_(catalog),
      factory_(std::move(factory)),
      render_(render),
      listener_(listener),
      logger_(logger) {}

EngineOutputDevices::~EngineOutputDevices() {
    close_output();
}

std::vector<cp::OutputDevice> EngineOutputDevices::list_outputs() {
    auto listed = catalog_.list(ac::AudioDeviceDirection::kOutput);
    if (!listed) {
        logger_.log(common::LogLevel::kWarning, "出力デバイスの一覧を取得できませんでした");
        return {};
    }
    std::vector<cp::OutputDevice> devices;
    devices.reserve(listed.value().size());
    for (auto& device : listed.value()) {
        devices.push_back(cp::OutputDevice{std::move(device.id), std::move(device.name)});
    }
    return devices;
}

common::Result<std::string, cp::ErrorCode> EngineOutputDevices::open_output(
    const std::string& device_id) {
    // ミキサーを 2 つのオーディオスレッドから呼ばないよう、開いている出力は先に止める。
    close_output();
    auto output = factory_(device_id);
    if (!output) {
        logger_.log(common::LogLevel::kWarning, "出力デバイスを開けませんでした");
        return common::make_unexpected(cp::ErrorCode::kDeviceUnavailable);
    }
    if (const auto started = output.value()->start(render_, listener_); !started) {
        logger_.log(common::LogLevel::kWarning, "出力デバイスで出力を始められませんでした");
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

void EngineOutputDevices::close_output() {
    if (output_) {
        output_->stop();
        output_.reset();
    }
}

}  // namespace vocroz::engine
