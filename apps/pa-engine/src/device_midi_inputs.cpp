// SPDX-License-Identifier: AGPL-3.0-only
//
// DeviceMidiInputs の実装。

#include "device_midi_inputs.h"

#include <iterator>
#include <utility>
#include <vector>

namespace vocroz::engine {

// 開いているポート 1 つ。JUCE
// の受信スレッドからメッセージを、メッセージスレッドから抜去を受け取る。
struct DeviceMidiInputs::Port final : audio_core::MidiInputCallback,
                                      audio_core::AudioDeviceListener {
    Port(std::string id_in, std::unique_ptr<audio_core::MidiInput> input_in,
         DeviceMidiListener& listener_in)
        : id(std::move(id_in)),
          input(std::move(input_in)),
          queue(std::make_shared<audio_core::MidiInputQueue>()),
          listener(listener_in) {}

    void on_midi_message(const audio_core::TimestampedMidiMessage& message) noexcept override {
        // 満杯なら捨てる。止める側のメッセージを落としたときは、キューが自動で Panic にする。
        static_cast<void>(queue->push(message));
    }

    void on_device_lost() noexcept override {
        if (lost) {
            return;
        }
        lost = true;
        // 抜いたときに押さえていた鍵盤の音を止める。別のポートへは切り替えない。
        queue->request_panic();
        try {
            listener.on_midi_device_closed(id);
        } catch (...) {
            // 通知できなくても Panic は求めてあるので、音は残らない。グラフから外れるのが
            // 次の refresh() まで遅れるだけ（キューは空のまま取り出される）。
        }
    }

    std::string id;
    std::unique_ptr<audio_core::MidiInput> input;
    std::shared_ptr<audio_core::MidiInputQueue> queue;
    DeviceMidiListener& listener;
    // 抜去を知らせた。ポートの後始末は次の refresh() で行う（通知の中で自分を破棄しないため）。
    bool lost = false;
};

DeviceMidiInputs::DeviceMidiInputs(Ports ports) : ports_(std::move(ports)) {}

DeviceMidiInputs::~DeviceMidiInputs() {
    close_all();
}

void DeviceMidiInputs::refresh() {
    // 抜去を知らせたポートは、もう知らせ済みなので閉じるだけにする。
    std::erase_if(open_, [](auto& entry) {
        if (!entry.second->lost) {
            return false;
        }
        entry.second->input->stop();
        return true;
    });

    const auto listed = ports_.catalog.list();
    if (!listed) {
        ports_.logger.log(common::LogLevel::kWarning, "MIDI 入力ポートの一覧を取得できません");
        return;
    }
    std::set<std::string, std::less<>> present;
    for (const auto& info : listed.value()) {
        present.insert(info.id);
    }
    // 抜去の通知が届かないまま一覧から消えたポートも、抜去と同じに扱う。
    for (auto it = open_.begin(); it != open_.end();) {
        if (present.contains(it->first)) {
            ++it;
            continue;
        }
        const auto next = std::next(it);
        ports_.listener.on_midi_device_closed(it->first);
        close(it);
        it = next;
    }
    std::erase_if(failed_, [&present](const std::string& id) { return !present.contains(id); });

    for (const auto& info : listed.value()) {
        if (open_.contains(info.id) || failed_.contains(info.id)) {
            continue;
        }
        auto input = ports_.open(info);
        if (!input) {
            failed_.insert(info.id);
            continue;
        }
        auto port = std::make_unique<Port>(info.id, std::move(input), ports_.listener);
        if (!port->input->start(*port, *port)) {
            ports_.logger.log(common::LogLevel::kWarning,
                              "MIDI 入力ポート「" + info.name + "」を開けませんでした");
            failed_.insert(info.id);
            continue;
        }
        ports_.logger.log(common::LogLevel::kInfo,
                          "MIDI 入力ポート「" + info.name + "」を開きました");
        auto queue = port->queue;
        open_.emplace(info.id, std::move(port));
        ports_.listener.on_midi_device_opened(info.id, std::move(queue));
    }
}

void DeviceMidiInputs::close_all() {
    while (!open_.empty()) {
        close(open_.begin());
    }
}

std::size_t DeviceMidiInputs::open_count() const noexcept {
    return open_.size();
}

void DeviceMidiInputs::close(PortMap::iterator port) {
    port->second->queue->request_panic();
    port->second->input->stop();
    open_.erase(port);
}

}  // namespace vocroz::engine
