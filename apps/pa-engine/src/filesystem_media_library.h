// SPDX-License-Identifier: AGPL-3.0-only
//
// session の MediaLibrary の実装。メディアフォルダの音声ファイルを std::filesystem で探す
// （docs/adr/0043-recording-and-media-player.md）。
// メディアプレイヤーが開くのは、ここで作った一覧のファイルだけ。UI から任意のパスを受け取らず、
// メディアフォルダの外（".." やリンクの先）を開かないため。
#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "vocroz/session/ports/media_library.h"

namespace vocroz::engine {

/// std::filesystem によるメディアフォルダの一覧。
/// スレッド安全性: コントロールスレッドから呼ぶ。フォルダの走査でブロックしうる。
class FilesystemMediaLibrary final : public session::MediaLibrary {
public:
    [[nodiscard]] std::vector<std::string> list(const std::string& folder,
                                                std::size_t limit) override;
    [[nodiscard]] std::string resolve(const std::string& folder,
                                      const std::string& relative_path) const override;
};

}  // namespace vocroz::engine
