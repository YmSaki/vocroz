// SPDX-License-Identifier: AGPL-3.0-only
//
// 埋め込んだ UI のファイルの検索と MIME タイプの決定。

#include "vocroz/webui/ui_resource.h"

#include <algorithm>
#include <array>
#include <utility>

namespace vocroz::webui {
namespace {

// Vite のビルド成果物に現れる拡張子だけを並べる。テキストには charset を付け、
// WebView が OS の既定の文字コードで日本語を読み違えないようにする。
constexpr std::array<std::pair<std::string_view, std::string_view>, 13> kMimeTypes{{
    {".html", "text/html; charset=utf-8"},
    {".js", "text/javascript; charset=utf-8"},
    {".mjs", "text/javascript; charset=utf-8"},
    {".css", "text/css; charset=utf-8"},
    {".json", "application/json; charset=utf-8"},
    {".map", "application/json; charset=utf-8"},
    {".svg", "image/svg+xml"},
    {".png", "image/png"},
    {".ico", "image/x-icon"},
    {".webp", "image/webp"},
    {".woff2", "font/woff2"},
    {".woff", "font/woff"},
    {".txt", "text/plain; charset=utf-8"},
}};

constexpr std::string_view kDefaultMimeType = "application/octet-stream";
constexpr std::string_view kIndexPath = "index.html";

bool ends_with_ignoring_case(std::string_view text, std::string_view suffix) {
    if (text.size() < suffix.size()) {
        return false;
    }
    const auto tail = text.substr(text.size() - suffix.size());
    return std::equal(tail.begin(), tail.end(), suffix.begin(), [](char a, char b) {
        const auto lower = [](char c) {
            return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
        };
        return lower(a) == lower(b);
    });
}

// `..` のセグメントがあれば true。`a..b` のようなファイル名は通す。
bool has_parent_segment(std::string_view path) {
    std::size_t start = 0;
    while (start <= path.size()) {
        const std::size_t end = std::min(path.find('/', start), path.size());
        if (path.substr(start, end - start) == "..") {
            return true;
        }
        start = end + 1;
    }
    return false;
}

}  // namespace

std::string_view mime_type_for(std::string_view path) {
    for (const auto& [extension, mime_type] : kMimeTypes) {
        if (ends_with_ignoring_case(path, extension)) {
            return mime_type;
        }
    }
    return kDefaultMimeType;
}

std::optional<UiResource> find_ui_resource(UiAssetTable assets, std::string_view request_path) {
    std::string_view path = request_path.substr(0, request_path.find_first_of("?#"));
    while (!path.empty() && path.front() == '/') {
        path.remove_prefix(1);
    }
    if (path.empty()) {
        path = kIndexPath;
    }
    if (path.find('\\') != std::string_view::npos || has_parent_segment(path)) {
        return std::nullopt;
    }
    const auto found = std::find_if(assets.begin(), assets.end(),
                                    [&](const UiAsset& asset) { return asset.path == path; });
    if (found == assets.end()) {
        return std::nullopt;
    }
    return UiResource{found->data, mime_type_for(found->path)};
}

}  // namespace vocroz::webui
