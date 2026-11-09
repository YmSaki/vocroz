// SPDX-License-Identifier: AGPL-3.0-only
//
// 秘密情報の暗号化と復号。Windows では DPAPI（src/win/dpapi_data_protector.cpp）で実装し、
// テストではフェイク（tests/fakes/fake_data_protector.h）に差し替える。
// ファイルの置き場所とアトミックな保存（SecretFileStore）を OS の暗号化から切り離し、
// Linux の CI で保存の規則を単体テストできるようにするための、persistence の中だけの境界。
#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace vocroz::persistence {

/// 現在のユーザーだけが復号できる形への暗号化。
/// スレッド安全性: コントロールスレッドからだけ呼ぶ。
class DataProtector {
public:
    virtual ~DataProtector() = default;

    /// plain を暗号化する。
    /// @param plain   平文
    /// @param purpose 用途の名前。復号のときに同じ値を渡さないと復号できない。
    ///        用途の取り違えを防ぐため
    /// @return 暗号文。失敗したら std::nullopt
    [[nodiscard]] virtual std::optional<std::vector<char>> protect(
        std::span<const std::uint8_t> plain, std::string_view purpose) = 0;

    /// protect() の結果を復号する。
    /// @param sealed  暗号文
    /// @param purpose protect() に渡した用途の名前
    /// @return 平文。改ざん、別のユーザーや PC での暗号化、用途の違いでは std::nullopt
    [[nodiscard]] virtual std::optional<std::vector<std::uint8_t>> unprotect(
        std::span<const char> sealed, std::string_view purpose) = 0;
};

}  // namespace vocroz::persistence
