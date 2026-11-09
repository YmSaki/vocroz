// SPDX-License-Identifier: AGPL-3.0-only
//
// DataProtector の DPAPI（CryptProtectData）による実装。現在の Windows ユーザーの資格情報から
// 導かれる鍵で暗号化するので、同じユーザーでなければ復号できない。
#pragma once

#include "data_protector.h"

namespace vocroz::persistence {

/// DPAPI で暗号化と復号を行う。用途の名前は追加のエントロピーとして渡す。
/// スレッド安全性: どのスレッドからも呼べる。
/// 使うのは SecretFileStore と同じくコントロールスレッドから。
class DpapiDataProtector final : public DataProtector {
public:
    std::optional<std::vector<char>> protect(std::span<const std::uint8_t> plain,
                                             std::string_view purpose) override;
    std::optional<std::vector<std::uint8_t>> unprotect(std::span<const char> sealed,
                                                       std::string_view purpose) override;
};

}  // namespace vocroz::persistence
