// SPDX-License-Identifier: AGPL-3.0-only
//
// find_plugin_files()（plugin_file_finder.h）の実装。

#include "plugin_file_finder.h"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <system_error>

namespace vocroz::plugin_host {
namespace {

namespace fs = std::filesystem;

// 大文字の拡張子で配布されるプラグインもあるので、大文字と小文字を区別しない。
// Windows の path::string() は ANSI に変換できない文字で例外を送出するので、UTF-8 で比べる。
bool has_vst3_extension(const fs::path& path) {
    std::u8string extension = path.extension().u8string();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](char8_t c) {
        return static_cast<char8_t>(c >= u8'A' && c <= u8'Z' ? c - u8'A' + u8'a' : c);
    });
    return extension == u8".vst3";
}

std::string to_utf8(const fs::path& path) {
    const std::u8string text = path.u8string();
    return {text.begin(), text.end()};
}

std::int64_t to_ticks(fs::file_time_type time) {
    return static_cast<std::int64_t>(time.time_since_epoch().count());
}

// ファイル 1 つの更新日時とサイズ。読めなければ std::nullopt。
std::optional<session::PluginFile> stamp_file(const fs::path& path) {
    std::error_code error;
    const auto size = fs::file_size(path, error);
    if (error) {
        return std::nullopt;
    }
    const auto modified = fs::last_write_time(path, error);
    if (error) {
        return std::nullopt;
    }
    return session::PluginFile{to_utf8(path), to_ticks(modified), size};
}

// バンドルの中の通常のファイルの、最も新しい更新日時とサイズの合計。
session::PluginFile stamp_bundle(const fs::path& bundle) {
    session::PluginFile result{to_utf8(bundle), 0, 0};
    std::error_code error;
    const auto bundle_time = fs::last_write_time(bundle, error);
    if (!error) {
        result.modified_time = to_ticks(bundle_time);
    }
    fs::recursive_directory_iterator it(bundle, fs::directory_options::skip_permission_denied,
                                        error);
    for (; !error && it != fs::recursive_directory_iterator(); it.increment(error)) {
        std::error_code entry_error;
        if (!it->is_regular_file(entry_error) || entry_error) {
            continue;
        }
        if (const auto file = stamp_file(it->path())) {
            result.modified_time = std::max(result.modified_time, file->modified_time);
            result.size_bytes += file->size_bytes;
        }
    }
    return result;
}

void collect(const fs::path& directory, std::vector<session::PluginFile>& out) {
    std::error_code error;
    fs::recursive_directory_iterator it(directory, fs::directory_options::skip_permission_denied,
                                        error);
    for (; !error && it != fs::recursive_directory_iterator(); it.increment(error)) {
        if (!has_vst3_extension(it->path())) {
            continue;
        }
        std::error_code entry_error;
        if (it->is_directory(entry_error) && !entry_error) {
            out.push_back(stamp_bundle(it->path()));
            it.disable_recursion_pending();
        } else if (it->is_regular_file(entry_error) && !entry_error) {
            if (const auto file = stamp_file(it->path())) {
                out.push_back(*file);
            }
        }
    }
}

}  // namespace

std::vector<session::PluginFile> find_plugin_files(std::span<const fs::path> directories) {
    std::vector<session::PluginFile> files;
    for (const fs::path& directory : directories) {
        collect(directory, files);
    }
    std::sort(files.begin(), files.end(),
              [](const auto& a, const auto& b) { return a.path < b.path; });
    // 探す場所が重なっていると同じファイルを 2 回見つけるので、1 つにする。
    files.erase(std::unique(files.begin(), files.end(),
                            [](const auto& a, const auto& b) { return a.path == b.path; }),
                files.end());
    return files;
}

}  // namespace vocroz::plugin_host
