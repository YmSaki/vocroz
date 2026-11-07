// SPDX-License-Identifier: AGPL-3.0-only
//
// 「現行版」を指すポインターファイル（インストール先の
// current.txt）と、バージョン別フォルダの名前。 起動スタブと pa-updater
// が同じ規則で読み書きするため、形式はここだけで定義する
// （docs/adr/0004-side-by-side-updates.md、docs/adr/0009-installer-and-updater.md）。
#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "vocroz/update/semantic_version.h"

namespace vocroz::update {

/// ポインターファイルの内容。
struct InstallPointer {
    /// 現行版。
    SemanticVersion current;
    /// 直前の版。ロールバックと、起動スタブが現行版を起動できなかったときの代わりに使う。
    std::optional<SemanticVersion> previous;

    /// すべての要素が等しいとき true。
    friend bool operator==(const InstallPointer&, const InstallPointer&) = default;
};

/// ポインターファイルのファイル名。
inline constexpr std::string_view kInstallPointerFileName = "current.txt";

/// ポインターファイルのテキストを読む。形式は "current=<版>\n" と、任意の "previous=<版>\n"。
/// @return 形式が正しければ値。壊れていれば
/// std::nullopt（呼び出し側はポインターがないものとして扱う）。
///         スレッド安全。
[[nodiscard]] std::optional<InstallPointer> parse_install_pointer(std::string_view text);

/// ポインターファイルのテキストを作る。parse_install_pointer() の逆変換。
[[nodiscard]] std::string serialize_install_pointer(const InstallPointer& pointer);

/// バージョン別フォルダの名前（"app-0.1.0-alpha.2"）を返す。
[[nodiscard]] std::string version_directory_name(const SemanticVersion& version);

/// バージョン別フォルダの名前からバージョンを読む。
/// @return "app-<セマンティックバージョン>" の形式なら値。それ以外のフォルダは std::nullopt。
[[nodiscard]] std::optional<SemanticVersion> parse_version_directory_name(std::string_view name);

}  // namespace vocroz::update
