// SPDX-License-Identifier: AGPL-3.0-only
//
// 署名を検証した後の更新マニフェスト。ManifestVerifier だけが作り、
// 内側の処理（適用の判断、パッケージの検証）は検証済みのこの型だけを受け取る。
#pragma once

#include <array>
#include <cstdint>
#include <string>

#include "vocroz/update/semantic_version.h"

namespace vocroz::update {

/// SHA-256 のダイジェスト（32 バイト）。
using Sha256Digest = std::array<std::uint8_t, 32>;

/// 検証済みの更新マニフェスト。
struct UpdateManifest {
    /// 更新後のバージョン。
    SemanticVersion version;
    /// 更新パッケージ（ZIP）の HTTPS の URL。
    std::string package_url;
    /// 更新パッケージ全体の SHA-256。
    Sha256Digest package_sha256{};
    /// 更新パッケージのバイト数。
    std::uint64_t package_size = 0;
    /// 展開した実行ファイルの Authenticode の発行者（証明書のサブジェクト）。
    /// 空なら Authenticode を検証しない（署名証明書が届くまでの期間）。
    /// マニフェストの署名の対象なので、攻撃者が空に書き換えることはできない。
    std::string publisher;
};

}  // namespace vocroz::update
