// SPDX-License-Identifier: AGPL-3.0-only
//
// CodeSignatureVerifier の実装。WinVerifyTrust で Authenticode の信頼チェーンを確かめ、
// 署名者の証明書の名前（CN）がマニフェストの発行者と一致するかを照合する。
#pragma once

#include <filesystem>

#include "vocroz/update/ports/code_signature_verifier.h"

namespace vocroz::updater::win {

class WinTrustCodeSignatureVerifier final : public update::ports::CodeSignatureVerifier {
public:
    /// @param install_dir インストール先
    explicit WinTrustCodeSignatureVerifier(std::filesystem::path install_dir);

    [[nodiscard]] bool verify(const update::SemanticVersion& version,
                              std::string_view publisher) override;

private:
    std::filesystem::path install_dir_;
};

}  // namespace vocroz::updater::win
