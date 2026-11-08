// SPDX-License-Identifier: AGPL-3.0-only
//
// SessionMonitor の実装。pa-engine
// がセッションの稼働中に保持する名前付きミューテックスの有無を調べる。
#pragma once

#include <string>

#include "vocroz/update/ports/session_monitor.h"

namespace vocroz::updater::win {

class MutexSessionMonitor final : public update::ports::SessionMonitor {
public:
    /// @param data_dir_utf8 データフォルダ（UTF-8）。ミューテックスの名前の範囲を決める
    explicit MutexSessionMonitor(const std::string& data_dir_utf8);

    [[nodiscard]] bool is_session_active() override;

private:
    std::wstring mutex_name_;
};

}  // namespace vocroz::updater::win
