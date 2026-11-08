// SPDX-License-Identifier: AGPL-3.0-only
//
// UpdateSource のフェイク。マニフェストの内容と、ダウンロードの成否をテストが決める。
#pragma once

#include <optional>
#include <string>

#include "vocroz/update/ports/update_source.h"

namespace vocroz::update::fakes {

class FakeUpdateSource final : public ports::UpdateSource {
public:
    std::optional<std::string> fetch_manifest() override {
        return manifest;
    }

    bool download_package(const UpdateManifest& /*manifest*/) override {
        ++download_calls;
        return !fail_download;
    }

    bool store_manifest(std::string_view text) override {
        stored_manifest = std::string{text};
        return true;
    }

    std::optional<std::string> manifest;
    bool fail_download = false;
    int download_calls = 0;
    std::optional<std::string> stored_manifest;
};

}  // namespace vocroz::update::fakes
