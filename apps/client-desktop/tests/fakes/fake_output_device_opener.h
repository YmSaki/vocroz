// SPDX-License-Identifier: AGPL-3.0-only
//
// テスト用の OutputDeviceOpener。登録した ID のデバイスだけを FakeAudioOutput として開き、
// 最後に開いた出力を記録する（描画と抜去の模擬に使う）。
#pragma once

#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "fakes/fake_audio_output.h"
#include "output_device_opener.h"

namespace vocroz::client {

/// 登録した ID のデバイスだけを開く OutputDeviceOpener。
class FakeOutputDeviceOpener final : public OutputDeviceOpener {
public:
    [[nodiscard]] common::Result<std::unique_ptr<audio_core::AudioOutput>,
                                 audio_core::AudioDeviceError>
    open(std::string_view device_id) override {
        opened.emplace_back(device_id);
        if (!available.contains(std::string(device_id))) {
            return common::make_unexpected(audio_core::AudioDeviceError::kDeviceNotFound);
        }
        auto output = std::make_unique<audio_core::FakeAudioOutput>(channels);
        last = output.get();
        return std::unique_ptr<audio_core::AudioOutput>(std::move(output));
    }

    /// 開けるデバイスの ID。
    std::set<std::string> available;
    /// 開く出力のチャンネル数。
    std::size_t channels = 2;
    /// open() に渡された ID。
    std::vector<std::string> opened;
    /// 最後に開いた出力。閉じた後は指さないので、開いている間だけ使う。
    audio_core::FakeAudioOutput* last = nullptr;
};

}  // namespace vocroz::client
