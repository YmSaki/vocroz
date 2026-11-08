// SPDX-License-Identifier: AGPL-3.0-only
//
// セッションの稼働を調べるポート。実装は apps/updater/src/win/（session_lock_name()
// のミューテックス）。
#pragma once

namespace vocroz::update::ports {

/// セッションの稼働の監視。
class SessionMonitor {
public:
    virtual ~SessionMonitor() = default;

    /// いずれかのセッションが稼働中か。判定できないときは稼働中として扱う（フェイルセーフ）。
    /// @return 稼働中なら true。コントロールスレッドから呼ぶ。
    [[nodiscard]] virtual bool is_session_active() = 0;
};

}  // namespace vocroz::update::ports
