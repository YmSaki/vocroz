// SPDX-License-Identifier: AGPL-3.0-only
//
// AtomicFile の実装。

#include "atomic_file.h"

#include <fstream>
#include <iterator>
#include <system_error>
#include <utility>

#include "detail/durable_io.h"
#include "persistence_constants.h"

namespace vocroz::persistence {
namespace {

std::filesystem::path with_suffix(const std::filesystem::path& path, std::string_view suffix) {
    auto result = path;
    result += suffix;
    return result;
}

common::Result<std::optional<std::string>, AtomicFileError> read_small_file(
    const std::filesystem::path& path) {
    std::error_code ec;
    const auto status = std::filesystem::status(path, ec);
    if (status.type() == std::filesystem::file_type::not_found) {
        return std::optional<std::string>{};
    }
    if (ec || status.type() != std::filesystem::file_type::regular) {
        return common::make_unexpected(AtomicFileError::kIoError);
    }
    const auto size = std::filesystem::file_size(path, ec);
    if (ec) {
        return common::make_unexpected(AtomicFileError::kIoError);
    }
    if (size > kMaxSmallFileBytes) {
        return common::make_unexpected(AtomicFileError::kTooLarge);
    }
    std::ifstream in{path, std::ios::binary};
    if (!in) {
        return common::make_unexpected(AtomicFileError::kIoError);
    }
    std::string content{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
    if (in.bad()) {
        return common::make_unexpected(AtomicFileError::kIoError);
    }
    return std::optional<std::string>{std::move(content)};
}

}  // namespace

AtomicFile::AtomicFile(std::filesystem::path path)
    : path_(std::move(path)),
      temporary_path_(with_suffix(path_, kTemporarySuffix)),
      backup_path_(with_suffix(path_, kBackupSuffix)) {}

common::Result<std::optional<std::string>, AtomicFileError> AtomicFile::read_current() const {
    return read_small_file(path_);
}

common::Result<std::optional<std::string>, AtomicFileError> AtomicFile::read_backup() const {
    return read_small_file(backup_path_);
}

common::Result<void, AtomicFileError> AtomicFile::commit(std::span<const char> bytes,
                                                         bool keep_current_as_backup) {
    std::error_code ec;
    if (const auto parent = path_.parent_path(); !parent.empty()) {
        std::filesystem::create_directories(parent, ec);
        if (ec) {
            return common::make_unexpected(AtomicFileError::kIoError);
        }
    }
    // 1. 新しい内容を一時ファイルに書き切る。ここで止まっても本体とバックアップは変わらない。
    if (!detail::write_file_durably(temporary_path_, bytes)) {
        return common::make_unexpected(AtomicFileError::kIoError);
    }
    // 2. 今の本体をバックアップに回す。ここで止まると本体がない状態になるが、
    //    バックアップに直前の正常な内容があるので、読み込みはそちらから復元できる。
    if (keep_current_as_backup && std::filesystem::exists(path_, ec)) {
        if (!detail::replace_file_durably(path_, backup_path_)) {
            return common::make_unexpected(AtomicFileError::kIoError);
        }
    }
    // 3. 一時ファイルを本体に差し替える。
    if (!detail::replace_file_durably(temporary_path_, path_)) {
        return common::make_unexpected(AtomicFileError::kIoError);
    }
    return {};
}

common::Result<void, AtomicFileError> AtomicFile::remove_all() {
    bool failed = false;
    for (const auto* path : {&path_, &temporary_path_, &backup_path_}) {
        std::error_code ec;
        std::filesystem::remove(*path, ec);
        failed = failed || static_cast<bool>(ec);
    }
    if (failed) {
        return common::make_unexpected(AtomicFileError::kIoError);
    }
    return {};
}

}  // namespace vocroz::persistence
