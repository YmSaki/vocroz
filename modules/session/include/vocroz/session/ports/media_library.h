// SPDX-License-Identifier: AGPL-3.0-only
//
// メディアフォルダの音声ファイルの一覧を作るポート。pa-engine がファイルシステムで実装する
// （docs/adr/0043-recording-and-media-player.md）。
// メディアプレイヤーが読むファイルを一覧のものに限り、UI から任意のパスを受け取らないため。
#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace vocroz::session {

/// メディアフォルダの一覧。
/// スレッド安全性: コントロールスレッドからだけ呼ぶ。フォルダの走査でブロックしうる。
class MediaLibrary {
public:
    virtual ~MediaLibrary() = default;

    /// メディアフォルダの中の音声ファイル（.wav、.flac、.aif、.aiff、.ogg）を、サブフォルダを
    /// 2 段まで探して返す。".." を含むパスと、メディアフォルダの外を指すリンクは載せない。
    /// @param folder メディアフォルダ
    /// @param limit  返すファイルの数の上限
    /// @return メディアフォルダからの相対パス（区切りは "/"、昇順）。フォルダがなければ空
    [[nodiscard]] virtual std::vector<std::string> list(const std::string& folder,
                                                        std::size_t limit) = 0;

    /// 相対パスを、メディアプレイヤーに渡すファイルのパスにする。
    /// @param folder        メディアフォルダ
    /// @param relative_path list() が返した相対パス
    /// @return ファイルのパス
    [[nodiscard]] virtual std::string resolve(const std::string& folder,
                                              const std::string& relative_path) const = 0;
};

}  // namespace vocroz::session
