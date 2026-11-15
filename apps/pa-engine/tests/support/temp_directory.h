// SPDX-License-Identifier: AGPL-3.0-only
//
// テスト 1 つ分の一時フォルダ。テイクのフォルダとメディアフォルダの走査のテストに使う。
#pragma once

#include <atomic>
#include <filesystem>
#include <string>
#include <system_error>

#include <gtest/gtest.h>

namespace vocroz::engine {

/// テスト 1 つ分の一時フォルダ。破棄で中身ごと消す。
class TempDirectory {
public:
    TempDirectory() {
        static std::atomic<int> counter{0};
        const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
        std::string name = "vocroz-pa-engine-";
        if (info != nullptr) {
            name += std::string{info->test_suite_name()} + "-" + info->name();
        }
        name += "-" + std::to_string(counter++);
        path_ = std::filesystem::temp_directory_path() / name;
        std::filesystem::remove_all(path_);
        std::filesystem::create_directories(path_);
    }
    TempDirectory(const TempDirectory&) = delete;
    TempDirectory& operator=(const TempDirectory&) = delete;
    ~TempDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }

    [[nodiscard]] const std::filesystem::path& path() const noexcept {
        return path_;
    }

private:
    std::filesystem::path path_;
};

}  // namespace vocroz::engine
