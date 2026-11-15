// SPDX-License-Identifier: AGPL-3.0-only
//
// テスト用の MediaLibrary。フォルダごとに決めたファイルの一覧を返す。
#pragma once

#include <map>
#include <string>
#include <vector>

#include "vocroz/session/ports/media_library.h"

namespace vocroz::session {

/// 決めた一覧を返す MediaLibrary。
class FakeMediaLibrary final : public MediaLibrary {
public:
    std::vector<std::string> list(const std::string& folder, std::size_t limit) override {
        ++lists_;
        auto files = files_[folder];
        if (files.size() > limit) {
            files.resize(limit);
        }
        return files;
    }

    std::string resolve(const std::string& folder,
                        const std::string& relative_path) const override {
        return folder + "/" + relative_path;
    }

    /// フォルダの一覧を設定する。
    void set_files(const std::string& folder, std::vector<std::string> files) {
        files_[folder] = std::move(files);
    }
    [[nodiscard]] int lists() const noexcept {
        return lists_;
    }

private:
    std::map<std::string, std::vector<std::string>> files_;
    int lists_ = 0;
};

}  // namespace vocroz::session
