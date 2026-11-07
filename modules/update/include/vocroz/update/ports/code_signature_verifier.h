// SPDX-License-Identifier: AGPL-3.0-only
//
// 展開した実行ファイルの Authenticode を検証するポート。実装は
// apps/updater/src/win/（WinVerifyTrust）。
#pragma once

#include <string_view>

#include "vocroz/update/semantic_version.h"

namespace vocroz::update::ports {

/// コード署名の検証。
class CodeSignatureVerifier {
public:
    virtual ~CodeSignatureVerifier() = default;

    /// そのバージョンのフォルダにある .exe と .dll が、すべて publisher
    /// の証明書で署名されているか。
    /// @param version 検証するバージョン別フォルダ
    /// @param publisher 期待する発行者（証明書のサブジェクト）。空で呼ばれることはない
    /// @return すべて信頼できる署名なら true。コントロールスレッドから呼ぶ。
    [[nodiscard]] virtual bool verify(const SemanticVersion& version,
                                      std::string_view publisher) = 0;
};

}  // namespace vocroz::update::ports
