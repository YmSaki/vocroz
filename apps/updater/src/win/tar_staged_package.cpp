// SPDX-License-Identifier: AGPL-3.0-only
//
// tar_staged_package.h の実装。

#include "win/tar_staged_package.h"

#include <array>
#include <chrono>
#include <fstream>
#include <iterator>
#include <system_error>

#include <windows.h>

#include "vocroz/update/install_pointer.h"

#include "updater_constants.h"
#include "win/win32_process.h"

namespace vocroz::updater::win {
namespace {

namespace fs = std::filesystem;

// 展開の上限時間。数十 MiB の ZIP は数秒で展開できるが、遅いディスクと
// ウイルス対策ソフトのスキャンを見込んで十分に長くする。
constexpr std::chrono::minutes kExtractTimeout{5};

// 1 回に読むバイト数。
constexpr std::size_t kReadChunkSize = 64 * 1024;

// PATH を探さず System32 の tar.exe を使う。カレントフォルダなどに置かれた同名の
// 偽物を実行しないため。
[[nodiscard]] fs::path system_tar() {
    std::array<wchar_t, MAX_PATH> buffer{};
    const UINT length = ::GetSystemDirectoryW(buffer.data(), static_cast<UINT>(buffer.size()));
    if (length == 0 || length >= buffer.size()) {
        return {};
    }
    return fs::path{std::wstring{buffer.data(), length}} / L"tar.exe";
}

}  // namespace

TarStagedPackage::TarStagedPackage(fs::path updates_dir, fs::path install_dir)
    : updates_dir_(std::move(updates_dir)), install_dir_(std::move(install_dir)) {}

bool TarStagedPackage::read(const ChunkSink& sink) {
    std::ifstream file(updates_dir_ / kStagedPackageFileName, std::ios::binary);
    if (!file) {
        return false;
    }
    std::array<std::uint8_t, kReadChunkSize> buffer{};
    while (file) {
        file.read(reinterpret_cast<char*>(buffer.data()),
                  static_cast<std::streamsize>(buffer.size()));
        const auto count = static_cast<std::size_t>(file.gcount());
        if (count > 0) {
            sink(std::span<const std::uint8_t>{buffer.data(), count});
        }
    }
    return file.eof();
}

bool TarStagedPackage::extract(const update::SemanticVersion& version) {
    const fs::path tar = system_tar();
    if (tar.empty()) {
        return false;
    }
    const std::string name = update::version_directory_name(version);
    const fs::path target = install_dir_ / name;
    // 作業用のフォルダは "app-" で始まらない名前にし、起動スタブと古い版の掃除の対象から外す。
    const fs::path partial = install_dir_ / ("partial-" + name);
    std::error_code error;
    fs::remove_all(partial, error);
    fs::remove_all(target, error);
    if (!fs::create_directories(partial, error)) {
        return false;
    }
    const auto result = run_and_capture(
        tar,
        {L"-x", L"-f", (updates_dir_ / kStagedPackageFileName).wstring(), L"-C", partial.wstring()},
        kExtractTimeout);
    // 展開の途中で失敗したフォルダは、次の試行のために残さない。
    if (!result || result->exit_code != 0) {
        fs::remove_all(partial, error);
        return false;
    }
    fs::rename(partial, target, error);
    if (error) {
        fs::remove_all(partial, error);
        return false;
    }
    return true;
}

std::optional<std::string> TarStagedPackage::read_staged_manifest() const {
    std::ifstream file(updates_dir_ / kStagedManifestFileName, std::ios::binary);
    if (!file) {
        return std::nullopt;
    }
    return std::string{std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};
}

void TarStagedPackage::discard() {
    std::error_code ignored;
    fs::remove(updates_dir_ / kStagedPackageFileName, ignored);
    fs::remove(updates_dir_ / kStagedManifestFileName, ignored);
}

}  // namespace vocroz::updater::win
