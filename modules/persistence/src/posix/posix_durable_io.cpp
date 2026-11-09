// SPDX-License-Identifier: AGPL-3.0-only
//
// detail/durable_io.h の POSIX の実装。製品は Windows だけを対象にするが、
// アトミックな保存の規則を Linux の CI で単体テストするために置く（docs/adr/0027-persistence.md）。

#include <cerrno>
#include <cstdio>

#include <fcntl.h>
#include <unistd.h>

#include "detail/durable_io.h"

namespace vocroz::persistence::detail {
namespace {

// ファイル記述子を閉じ忘れないための RAII ラッパー。
class FileDescriptor {
public:
    explicit FileDescriptor(int fd) noexcept : fd_(fd) {}
    FileDescriptor(const FileDescriptor&) = delete;
    FileDescriptor& operator=(const FileDescriptor&) = delete;
    FileDescriptor(FileDescriptor&&) = delete;
    FileDescriptor& operator=(FileDescriptor&&) = delete;
    ~FileDescriptor() {
        if (fd_ >= 0) {
            ::close(fd_);
        }
    }

    [[nodiscard]] int get() const noexcept {
        return fd_;
    }

    [[nodiscard]] bool valid() const noexcept {
        return fd_ >= 0;
    }

private:
    int fd_;
};

// 名前の変更は親フォルダのエントリの変更なので、フォルダも fsync しないとディスクに届かない。
bool sync_parent_directory(const std::filesystem::path& path) noexcept {
    auto parent = path.parent_path();
    if (parent.empty()) {
        parent = ".";
    }
    const FileDescriptor dir{::open(parent.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC)};
    return dir.valid() && ::fsync(dir.get()) == 0;
}

}  // namespace

common::Result<void, IoError> write_file_durably(const std::filesystem::path& path,
                                                 std::span<const char> bytes) noexcept {
    const FileDescriptor file{::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600)};
    if (!file.valid()) {
        return common::make_unexpected(IoError::kFailed);
    }
    std::size_t written = 0;
    while (written < bytes.size()) {
        const auto n = ::write(file.get(), bytes.data() + written, bytes.size() - written);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return common::make_unexpected(IoError::kFailed);
        }
        written += static_cast<std::size_t>(n);
    }
    if (::fsync(file.get()) != 0) {
        return common::make_unexpected(IoError::kFailed);
    }
    return {};
}

common::Result<void, IoError> replace_file_durably(const std::filesystem::path& from,
                                                   const std::filesystem::path& to) noexcept {
    // rename(2) は to が既にあってもアトミックに置き換える。
    if (std::rename(from.c_str(), to.c_str()) != 0) {
        return common::make_unexpected(IoError::kFailed);
    }
    if (!sync_parent_directory(to)) {
        return common::make_unexpected(IoError::kFailed);
    }
    return {};
}

}  // namespace vocroz::persistence::detail
