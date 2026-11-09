// SPDX-License-Identifier: AGPL-3.0-only
//
// engine_attacher.h の実装。

#include "engine_attacher.h"

#include <utility>

#include "app_constants.h"

namespace vocroz::pa_desktop {

EngineAttacher::EngineAttacher(EngineConnector& connector, EngineLauncher& launcher,
                               const common::Clock& clock, EngineAttachListener& listener) noexcept
    : connector_(connector), launcher_(launcher), clock_(clock), listener_(listener) {}

void EngineAttacher::begin() noexcept {
    const auto now = clock_.now();
    attaching_ = true;
    launched_ = false;
    next_attempt_ = now;
    deadline_ = now + kEngineStartTimeout;
}

void EngineAttacher::tick() {
    if (!attaching_) {
        return;
    }
    const auto now = clock_.now();
    if (now < next_attempt_) {
        return;
    }
    auto channel = connector_.connect();
    if (channel) {
        attaching_ = false;
        listener_.on_engine_attached(std::move(channel).value());
        return;
    }
    if (channel.error() == ConnectError::kRefused) {
        // ほかのユーザーのパイプやなりすましには、待っても起動してもつながらない。
        fail(AttachFailure::kRefused);
        return;
    }
    if (!launched_) {
        launched_ = true;
        if (!launcher_.launch()) {
            fail(AttachFailure::kLaunchFailed);
            return;
        }
        // エンジンの起動（デバイスの列挙、鍵の生成）にかかる時間を、起動した時点から測る。
        deadline_ = now + kEngineStartTimeout;
    } else if (const auto code = launcher_.exit_code()) {
        // 3 は同じデータフォルダのエンジンがすでに動いている合図で、そちらにつなぎ続ける
        // （docs/adr/0028-engine-assembly.md）。それ以外は起動に失敗したエンジン。
        if (*code != kEngineExitAlreadyRunning) {
            fail(AttachFailure::kEngineExited);
            return;
        }
    }
    if (now >= deadline_) {
        fail(AttachFailure::kTimedOut);
        return;
    }
    next_attempt_ = now + kEngineConnectRetryInterval;
}

void EngineAttacher::fail(AttachFailure failure) {
    attaching_ = false;
    listener_.on_engine_attach_failed(failure);
}

}  // namespace vocroz::pa_desktop
