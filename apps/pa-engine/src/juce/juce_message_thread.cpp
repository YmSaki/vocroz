// SPDX-License-Identifier: AGPL-3.0-only
//
// JuceMessageThread の実装。

#include "juce/juce_message_thread.h"

#include <future>
#include <utility>

#include <juce_events/juce_events.h>

namespace vocroz::engine {

JuceMessageThread::~JuceMessageThread() {
    stop();
}

void JuceMessageThread::start() {
    if (running_) {
        return;
    }
    std::promise<void> ready;
    auto started = ready.get_future();
    thread_ = std::thread([&ready] {
        // ScopedJuceInitialiser_GUI が MessageManager
        // を作り、作ったスレッドがメッセージスレッドになる。
        // 初期化とループと終了を、同じスレッドで行う。
        juce::ScopedJuceInitialiser_GUI juce;
        ready.set_value();
        juce::MessageManager::getInstance()->runDispatchLoop();
    });
    started.wait();
    running_ = true;
}

bool JuceMessageThread::post(Task task) {
    // stop() の後に MessageManager を参照すると、呼んだスレッドで作り直してしまう。
    auto* const manager = juce::MessageManager::getInstanceWithoutCreating();
    if (!running_ || manager == nullptr || manager->hasStopMessageBeenSent()) {
        return false;
    }
    return juce::MessageManager::callAsync(std::move(task));
}

void JuceMessageThread::stop(const Task& last) {
    if (!running_.exchange(false)) {
        return;
    }
    auto* const manager = juce::MessageManager::getInstanceWithoutCreating();
    if (manager != nullptr) {
        // 積んだ順に実行されるので、last はループを抜ける前に実行される。
        if (last) {
            juce::MessageManager::callAsync(last);
        }
        manager->stopDispatchLoop();
    }
    thread_.join();
}

}  // namespace vocroz::engine
