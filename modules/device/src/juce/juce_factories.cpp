// SPDX-License-Identifier: AGPL-3.0-only
//
// device の JUCE のアダプターのファクトリー関数（factories.h）。VOCROZ_WITH_JUCE が ON のときだけ
// ビルドする。

#include <memory>
#include <string>

#include "vocroz/device/factories.h"

#include "juce/juce_midi_device_catalog.h"
#include "juce/juce_midi_input.h"

namespace vocroz::device {

std::unique_ptr<audio_core::MidiDeviceCatalog> make_juce_midi_device_catalog() {
    return std::make_unique<JuceMidiDeviceCatalog>();
}

std::unique_ptr<audio_core::MidiInput> make_juce_midi_input(std::string_view identifier,
                                                            const common::Clock& clock,
                                                            common::Logger& logger) {
    return std::make_unique<JuceMidiInput>(std::string(identifier), clock, logger);
}

}  // namespace vocroz::device
