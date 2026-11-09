// SPDX-License-Identifier: AGPL-3.0-only
//
// プラグインノードの失敗の印。オーディオスレッドが立て、コントロールスレッドが読む。
// グラフを差し替えても同じ印を引き継ぐので、失敗したプラグインはプラグインを読み込み直すまで
// 無音のままになり、失敗したことをオペレーターに知らせられる
// （docs/adr/0041-plugin-editor-and-failure-notice.md）。
#pragma once

#include <atomic>

#include "vocroz/audio_core/realtime.h"

namespace vocroz::audio_core {

/// プラグインノードの失敗の印。std::shared_ptr で NodeSpec::make_plugin() に渡す。
/// 一度立てたら下ろさない。プラグインを読み込み直すときは、新しい印を作る。
/// スレッド安全性: raise() はオーディオスレッドから、raised() はどのスレッドからも呼び出し可能。
class PluginFault {
public:
    /// 失敗の印を立てる。立っていれば何もしない。
    /// スレッド安全性: オーディオスレッドから呼び出し可能。ブロックせず、ヒープを確保しない。
    void raise() noexcept VOCROZ_NONBLOCKING;

    /// @return 印が立っていれば true
    /// スレッド安全性: どのスレッドからも呼び出し可能。オーディオスレッドからも呼べる。
    [[nodiscard]] bool raised() const noexcept VOCROZ_NONBLOCKING;

private:
    std::atomic<bool> raised_{false};
};

}  // namespace vocroz::audio_core
