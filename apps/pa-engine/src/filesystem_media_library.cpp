// SPDX-License-Identifier: AGPL-3.0-only
//
// filesystem_media_library.h の実装。

#include "filesystem_media_library.h"

#include <algorithm>
#include <array>
#include <filesystem>
#include <string_view>
#include <system_error>

#include "vocroz/control_protocol/constants.h"

#include "utf8_path.h"

namespace vocroz::engine {
namespace {

namespace fs = std::filesystem;

/// 中まで探すサブフォルダの段数。メディアフォルダの直下と、2 段下のフォルダの中までを探す
/// （テイクのフォルダや、曲ごと、日ごとに分けたフォルダ）。深く探すと、フォルダを間違えたときに
/// 走査が長引くため。
constexpr int kMaxDepth = 2;

/// メディアプレイヤーが読める形式の拡張子（小文字）。
constexpr std::array<std::string_view, 5> kExtensions{".wav", ".flac", ".aif", ".aiff", ".ogg"};

bool is_audio_file(const fs::path& path) {
    std::string extension = path_utf8(path.extension());
    std::ranges::transform(extension, extension.begin(), [](char c) {
        return static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
    });
    return std::ranges::find(kExtensions, extension) != kExtensions.end();
}

}  // namespace

std::vector<std::string> FilesystemMediaLibrary::list(const std::string& folder,
                                                      std::size_t limit) {
    std::vector<std::string> files;
    const fs::path root = utf8_path(folder);
    std::error_code error;
    // リンクはたどらない。メディアフォルダの外のファイルを一覧に載せないため。
    fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, error);
    if (error) {
        return files;
    }
    for (const fs::recursive_directory_iterator end; it != end; it.increment(error)) {
        if (error) {
            break;
        }
        const auto& entry = *it;
        if (entry.is_symlink(error)) {
            it.disable_recursion_pending();
            continue;
        }
        if (entry.is_directory(error)) {
            if (it.depth() >= kMaxDepth) {
                it.disable_recursion_pending();
            }
            continue;
        }
        if (!entry.is_regular_file(error) || !is_audio_file(entry.path())) {
            continue;
        }
        // 区切りは UI と control-protocol に合わせて "/" にする。
        const std::u8string generic = entry.path().lexically_relative(root).generic_u8string();
        const std::string relative(generic.begin(), generic.end());
        if (relative.empty() || relative.size() > control_protocol::kMaxMediaPathBytes) {
            continue;
        }
        files.push_back(relative);
    }
    // 並びは UI の表示の順。テイクのフォルダは日時の名前なので、名前順で録った順に並ぶ。
    std::ranges::sort(files);
    if (files.size() > limit) {
        files.resize(limit);
    }
    return files;
}

std::string FilesystemMediaLibrary::resolve(const std::string& folder,
                                            const std::string& relative_path) const {
    return path_utf8(utf8_path(folder) / utf8_path(relative_path));
}

}  // namespace vocroz::engine
