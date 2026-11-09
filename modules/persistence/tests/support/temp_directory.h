// SPDX-License-Identifier: AGPL-3.0-only
//
// テストごとに空の一時フォルダを作り、終わったら消す。
// 実際のファイルシステムで、書き込み途中の中断や壊れたファイルを再現するために使う。
#pragma once

#include <atomic>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <system_error>

#include <gtest/gtest.h>

namespace vocroz::persistence {

/// テスト 1 つ分の一時フォルダ。
class TempDirectory {
public:
    TempDirectory() {
        static std::atomic<int> counter{0};
        const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
        std::string name = "vocroz-persistence-";
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
        std::error_code ec;
        std::filesystem::remove_all(path_, ec);
    }

    [[nodiscard]] const std::filesystem::path& path() const noexcept {
        return path_;
    }

private:
    std::filesystem::path path_;
};

/// path に text をそのまま書く（壊れたファイルや書きかけのファイルを置くため）。
inline void write_text(const std::filesystem::path& path, std::string_view text) {
    std::ofstream out{path, std::ios::binary | std::ios::trunc};
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
}

/// path の内容を返す。なければ空文字列。
inline std::string read_text(const std::filesystem::path& path) {
    std::ifstream in{path, std::ios::binary};
    return std::string{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
}

}  // namespace vocroz::persistence
