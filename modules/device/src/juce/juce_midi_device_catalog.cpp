// SPDX-License-Identifier: AGPL-3.0-only
//
// JuceMidiDeviceCatalog の実装。

#include "juce/juce_midi_device_catalog.h"

#include <juce_audio_devices/juce_audio_devices.h>

namespace vocroz::device {

common::Result<std::vector<audio_core::MidiDeviceInfo>, audio_core::AudioDeviceError>
JuceMidiDeviceCatalog::list() {
    std::vector<audio_core::MidiDeviceInfo> devices;
    for (const juce::MidiDeviceInfo& info : juce::MidiInput::getAvailableDevices()) {
        devices.push_back({info.identifier.toStdString(), info.name.toStdString()});
    }
    return devices;
}

}  // namespace vocroz::device
