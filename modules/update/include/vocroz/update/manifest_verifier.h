// SPDX-License-Identifier: AGPL-3.0-only
//
// 更新マニフェストの信頼境界。署名（Ed25519）を確かめてから形式を検証し、
// 検証済みの UpdateManifest に変換する。形式は docs/adr/0013-installer-and-updater.md。
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>
#include <variant>

#include "vocroz/update/update_manifest.h"

namespace vocroz::update {

/// Ed25519 の公開鍵（32 バイト）。
using Ed25519PublicKey = std::array<std::uint8_t, 32>;

/// マニフェストを拒否した理由。
enum class ManifestError {
    /// 公開鍵が埋め込まれていない。鍵のないビルドはどの更新も受け付けない（フェイルセーフ）。
    kNoPublicKey,
    /// 行の数、キーの順序、署名の行の形式が正しくない。
    kMalformed,
    /// 署名が一致しない（改ざん、または別の鍵での署名）。
    kBadSignature,
    /// format が対応していない値。
    kUnsupportedFormat,
    /// product が VOCROZ ではない。
    kWrongProduct,
    /// version、sha256、size のいずれかの形式が正しくない。
    kInvalidField,
    /// url が https:// ではない。
    kInsecureUrl,
    /// size が上限を超えている。
    kTooLarge,
};

/// 検証の結果。成功なら検証済みのマニフェスト、失敗なら拒否の理由。
using ManifestResult = std::variant<UpdateManifest, ManifestError>;

/// 署名付きの更新マニフェストを検証する。
class ManifestVerifier {
public:
    /// @param public_key 埋め込まれた公開鍵。std::nullopt なら verify() は常に kNoPublicKey
    /// を返す。
    explicit ManifestVerifier(std::optional<Ed25519PublicKey> public_key);

    /// マニフェストのテキストを検証する。署名を先に確かめ、署名が正しいものだけ中身を読む。
    /// 署名のないデータをパースしないことで、パーサーの攻撃面を鍵の持ち主にだけ限るため。
    /// @param text ダウンロードしたマニフェスト全体
    /// @return 検証済みのマニフェストか、拒否の理由。コントロールスレッドから呼ぶ。
    ///         状態を持たないので、どのスレッドから呼んでもよい。
    [[nodiscard]] ManifestResult verify(std::string_view text) const;

private:
    std::optional<Ed25519PublicKey> public_key_;
};

/// ビルド時に埋め込む公開鍵の 16 進数表記（64 桁の小文字）を変換する。
/// @return 形式が正しければ鍵。空文字列や不正な形式なら std::nullopt（更新を受け付けない）。
[[nodiscard]] std::optional<Ed25519PublicKey> parse_public_key_hex(std::string_view hex);

}  // namespace vocroz::update
