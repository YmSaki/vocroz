// SPDX-License-Identifier: AGPL-3.0-only
//
// セマンティックバージョン（Semantic Versioning 2.0.0）の値型とパース。
// 更新マニフェストの版、バージョン別フォルダの名前、ポインターファイルの版を、
// すべて同じ規則で比較するためにここへ集約する。
#pragma once

#include <compare>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace vocroz::update {

/// MAJOR.MINOR.PATCH[-PRERELEASE] 形式のバージョン。ビルドメタデータ（+...）は扱わない。
/// バージョン別フォルダの名前に使うため、比較で無視されるビルドメタデータを許すと
/// 「比較では同じ版なのにフォルダが別」という状態が生まれるため。
struct SemanticVersion {
    std::uint32_t major = 0;
    std::uint32_t minor = 0;
    std::uint32_t patch = 0;
    /// プレリリース識別子（例: "alpha.2"）。正式版は空。
    std::string prerelease;

    /// Semantic Versioning 2.0.0 の優先順位で比較する。
    /// 正式版は同じ番号のプレリリースより新しい。プレリリースは識別子ごとに、
    /// 数値は数値として、英数字は ASCII 順で比べ、数値は英数字より古い。
    /// @return 優先順位の比較結果。スレッド安全。
    friend std::strong_ordering operator<=>(const SemanticVersion& lhs, const SemanticVersion& rhs);
    /// すべての要素が等しいとき true。
    friend bool operator==(const SemanticVersion& lhs, const SemanticVersion& rhs) = default;
};

/// 文字列をセマンティックバージョンとして検証し、変換する。
/// @param text "0.1.0" や "0.1.0-alpha.2" の形式の文字列
/// @return 形式が正しければ値。先頭のゼロ、空の識別子、範囲外の数値、ビルドメタデータ、
///         前後の空白などを含む場合は std::nullopt。スレッド安全。
[[nodiscard]] std::optional<SemanticVersion> parse_semantic_version(std::string_view text);

/// "MAJOR.MINOR.PATCH[-PRERELEASE]" の文字列に戻す。parse_semantic_version() の逆変換。
/// @return バージョンの文字列。スレッド安全。
[[nodiscard]] std::string to_string(const SemanticVersion& version);

}  // namespace vocroz::update
