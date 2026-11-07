// SPDX-License-Identifier: AGPL-3.0-only
//
// InstallLayout のフェイク。バージョン別フォルダとポインターファイルをメモリ上で表す。
#pragma once

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

#include "vocroz/update/ports/install_layout.h"

namespace vocroz::update::fakes {

class FakeInstallLayout final : public ports::InstallLayout {
public:
    std::optional<std::string> read_pointer() override {
        return pointer;
    }

    bool write_pointer(std::string_view text) override {
        if (fail_write_pointer) {
            return false;
        }
        pointer = std::string{text};
        return true;
    }

    std::vector<SemanticVersion> installed_versions() override {
        return versions;
    }

    bool remove_version(const SemanticVersion& version) override {
        const auto it = std::find(versions.begin(), versions.end(), version);
        if (it == versions.end()) {
            return false;
        }
        versions.erase(it);
        return true;
    }

    [[nodiscard]] bool has(const SemanticVersion& version) const {
        return std::find(versions.begin(), versions.end(), version) != versions.end();
    }

    std::optional<std::string> pointer;
    std::vector<SemanticVersion> versions;
    bool fail_write_pointer = false;
};

}  // namespace vocroz::update::fakes
