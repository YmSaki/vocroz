// SPDX-License-Identifier: AGPL-3.0-only
//
// JuceMidiInput の実装。

#include "juce/juce_midi_input.h"

#include <span>
#include <utility>

#include "detail/midi_input_utils.h"

namespace vocroz::device {

namespace {

// 一覧にあるポートなら true。一覧にない ID で openDevice() を呼ばないために使う。
bool is_available(const std::string& identifier) {
    for (const juce::MidiDeviceInfo& info : juce::MidiInput::getAvailableDevices()) {
        if (info.identifier.toStdString() == identifier) {
            return true;
        }
    }
    return false;
}

}  // namespace

JuceMidiInput::JuceMidiInput(std::string identifier, const common::Clock& clock,
                             common::Logger& logger)
    : identifier_(std::move(identifier)), clock_(clock), logger_(logger) {}

JuceMidiInput::~JuceMidiInput() {
    stop();
}

common::Result<void, audio_core::AudioDeviceError> JuceMidiInput::start(
    audio_core::MidiInputCallback& callback, audio_core::AudioDeviceListener& listener) {
    if (device_) {
        return common::make_unexpected(audio_core::AudioDeviceError::kAlreadyStarted);
    }
    // 別のポートへのフォールバックはしない。
    if (!is_available(identifier_)) {
        return common::make_unexpected(audio_core::AudioDeviceError::kDeviceNotFound);
    }
    lost_.store(false, std::memory_order_relaxed);
    callback_.store(&callback, std::memory_order_release);
    listener_ = &listener;
    device_ = juce::MidiInput::openDevice(juce::String(identifier_), this);
    if (!device_) {
        // 一覧にはあるのに開けないのは、他のアプリケーションが排他で使っているとき。
        logger_.log(common::LogLevel::kWarning, "MIDI 入力ポートを開けなかった");
        callback_.store(nullptr, std::memory_order_release);
        listener_ = nullptr;
        return common::make_unexpected(audio_core::AudioDeviceError::kDeviceBusy);
    }
    device_->addDisconnectionListener(*this);
    device_->start();
    return {};
}

void JuceMidiInput::stop() noexcept {
    if (device_) {
        device_->removeDisconnectionListener(*this);
        // 止めてから破棄する。破棄が戻った時点で、受信スレッドからのコールバックは終わっている。
        device_->stop();
        device_.reset();
    }
    callback_.store(nullptr, std::memory_order_release);
    listener_ = nullptr;
}

void JuceMidiInput::handleIncomingMidiMessage(juce::MidiInput* /*source*/,
                                              const juce::MidiMessage& message) {
    auto* const callback = callback_.load(std::memory_order_acquire);
    if (callback == nullptr || lost_.load(std::memory_order_acquire)) {
        return;
    }
    // JUCE のタイムスタンプは Time::getMillisecondCounterHiRes() を秒にした値。
    const auto timestamp = detail::to_clock_time(
        clock_.now(), juce::Time::getMillisecondCounterHiRes(), message.getTimeStamp() * 1000.0);
    const std::span<const std::uint8_t> bytes(message.getRawData(),
                                              static_cast<std::size_t>(message.getRawDataSize()));
    if (const auto converted = detail::to_timestamped_message(bytes, timestamp)) {
        callback->on_midi_message(*converted);
    }
}

void JuceMidiInput::disconnected() {
    if (lost_.exchange(true, std::memory_order_acq_rel) || listener_ == nullptr) {
        return;
    }
    logger_.log(common::LogLevel::kWarning, "MIDI 入力ポートが抜去された");
    listener_->on_device_lost();
}

}  // namespace vocroz::device
