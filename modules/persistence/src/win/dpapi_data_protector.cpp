// SPDX-License-Identifier: AGPL-3.0-only
//
// DpapiDataProtector の実装。

#include "win/dpapi_data_protector.h"

#include <string>

// dpapi.h は windows.h の型を前提にするので、並べ替えさせずに windows.h を先に読む。
// clang-format off
#include <windows.h>
#include <dpapi.h>
// clang-format on

namespace vocroz::persistence {
namespace {

// 用途の名前に付ける接頭辞。
// 他のアプリが同じ用途の名前で DPAPI を使っていても、取り違えないようにする。
constexpr std::string_view kEntropyPrefix = "VOCROZ secret v1:";

// CryptProtectData が LocalAlloc で確保した出力を、LocalFree し忘れないための RAII ラッパー。
class LocalBlob {
public:
    LocalBlob() noexcept = default;
    LocalBlob(const LocalBlob&) = delete;
    LocalBlob& operator=(const LocalBlob&) = delete;
    LocalBlob(LocalBlob&&) = delete;
    LocalBlob& operator=(LocalBlob&&) = delete;
    ~LocalBlob() {
        if (blob_.pbData != nullptr) {
            // 復号した平文が残らないよう、解放の前にゼロで上書きする。
            SecureZeroMemory(blob_.pbData, blob_.cbData);
            LocalFree(blob_.pbData);
        }
    }

    [[nodiscard]] DATA_BLOB* get() noexcept {
        return &blob_;
    }

    [[nodiscard]] const DATA_BLOB& blob() const noexcept {
        return blob_;
    }

private:
    DATA_BLOB blob_{0, nullptr};
};

std::string make_entropy(std::string_view purpose) {
    std::string entropy{kEntropyPrefix};
    entropy += purpose;
    return entropy;
}

DATA_BLOB as_blob(const void* data, std::size_t size) noexcept {
    // DPAPI の入力は書き換えられないが、API の型が const でないためにキャストする。
    return DATA_BLOB{static_cast<DWORD>(size), static_cast<BYTE*>(const_cast<void*>(data))};
}

}  // namespace

std::optional<std::vector<char>> DpapiDataProtector::protect(std::span<const std::uint8_t> plain,
                                                             std::string_view purpose) {
    auto input = as_blob(plain.data(), plain.size());
    const auto entropy_text = make_entropy(purpose);
    auto entropy = as_blob(entropy_text.data(), entropy_text.size());
    LocalBlob output;
    // UI_FORBIDDEN: エンジンは画面を持たないので、DPAPI に確認の画面を出させない。
    if (!CryptProtectData(&input, nullptr, &entropy, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN,
                          output.get())) {
        return std::nullopt;
    }
    const auto* begin = reinterpret_cast<const char*>(output.blob().pbData);
    return std::vector<char>(begin, begin + output.blob().cbData);
}

std::optional<std::vector<std::uint8_t>> DpapiDataProtector::unprotect(std::span<const char> sealed,
                                                                       std::string_view purpose) {
    auto input = as_blob(sealed.data(), sealed.size());
    const auto entropy_text = make_entropy(purpose);
    auto entropy = as_blob(entropy_text.data(), entropy_text.size());
    LocalBlob output;
    if (!CryptUnprotectData(&input, nullptr, &entropy, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN,
                            output.get())) {
        return std::nullopt;
    }
    const auto* begin = output.blob().pbData;
    return std::vector<std::uint8_t>(begin, begin + output.blob().cbData);
}

}  // namespace vocroz::persistence
