// SPDX-License-Identifier: AGPL-3.0-only
//
// SettingsBackup のフェイク。退避と復元の呼び出しを記録する。
#pragma once

#include "vocroz/update/ports/settings_backup.h"

namespace vocroz::update::fakes {

class FakeSettingsBackup final : public ports::SettingsBackup {
public:
    bool backup() override {
        ++backup_calls;
        return !fail_backup;
    }

    bool restore() override {
        ++restore_calls;
        return true;
    }

    bool fail_backup = false;
    int backup_calls = 0;
    int restore_calls = 0;
};

}  // namespace vocroz::update::fakes
