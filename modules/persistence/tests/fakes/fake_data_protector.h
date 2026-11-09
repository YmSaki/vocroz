// SPDX-License-Identifier: AGPL-3.0-only
//
// テスト用の DataProtector。用途の名前を先頭に付けて各バイトを反転するだけの、決定的な「暗号化」。
// 用途の取り違えと改ざんで復号に失敗する振る舞いだけを DPAPI と揃える。
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "data_protector.h"

namespace vocroz::persistence {

/// 決定的に動く DataProtector。fail_protect() で暗号化の失敗も再現できる。
class FakeDataProtector final : public DataProtector {
public:
    std::optional<std::vector<char>> protect(std::span<const std::uint8_t> plain,
                                             std::string_view purpose) override {
        if (fail_protect_) {
            return std::nullopt;
        }
        std::vector<char> sealed{purpose.begin(), purpose.end()};
        sealed.push_back('|');
        for (const auto byte : plain) {
            sealed.push_back(static_cast<char>(~byte));
        }
        return sealed;
    }

    std::optional<std::vector<std::uint8_t>> unprotect(std::span<const char> sealed,
                                                       std::string_view purpose) override {
        const std::string prefix = std::string{purpose} + "|";
        if (sealed.size() < prefix.size() ||
            std::string_view{sealed.data(), prefix.size()} != prefix) {
            return std::nullopt;
        }
        std::vector<std::uint8_t> plain;
        for (auto it = sealed.begin() + static_cast<std::ptrdiff_t>(prefix.size());
             it != sealed.end(); ++it) {
            plain.push_back(static_cast<std::uint8_t>(~static_cast<std::uint8_t>(*it)));
        }
        return plain;
    }

    /// 以降の protect() を失敗させる。
    void fail_protect() noexcept {
        fail_protect_ = true;
    }

private:
    bool fail_protect_ = false;
};

}  // namespace vocroz::persistence
