// SPDX-License-Identifier: AGPL-3.0-only
//
// 更新マニフェストのテキスト形式のパース。署名の検証は ManifestVerifier が行い、
// ここはバイト列を「署名対象の本文と署名」に分けることと、本文を値に変換することだけを担う。
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

#include "vocroz/update/manifest_verifier.h"

namespace vocroz::update::wire {

/// 署名対象の本文と、その Ed25519 署名。
struct SignedManifestText {
    /// 最後の signature 行より前のすべてのバイト（末尾の改行を含む）。
    std::string_view body;
    /// Ed25519 の署名（64 バイト）。
    std::array<std::uint8_t, 64> signature{};
};

/// マニフェスト全体を、署名対象の本文と署名に分ける。
/// @return 最後の行が "signature=<128 桁の小文字の 16 進数>" で、その前に本文があれば値。
///         それ以外は std::nullopt。
[[nodiscard]] std::optional<SignedManifestText> split_signed_manifest(std::string_view text);

/// 署名を確かめた後の本文を、検証済みのマニフェストに変換する。
/// キーは format、product、version、url、sha256、size、publisher の順に 1 回ずつ並ぶ。
/// 順序を固定するのは、同じ内容に複数の表記を許さず、署名する側と読む側の解釈を一致させるため。
/// @return 検証済みのマニフェストか、拒否の理由。
[[nodiscard]] ManifestResult decode_manifest_body(std::string_view body);

}  // namespace vocroz::update::wire
