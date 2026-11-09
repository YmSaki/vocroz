// SPDX-License-Identifier: AGPL-3.0-only
//
// SRTP 外部出力のマスターキー（マスターキー 16 バイトとマスターソルト 14 バイト）。
// 鍵は SDES（RFC 4568）の a=crypto 属性の inline: と同じ形式（30 バイトの Base64）で受け渡しする。
// 受信機の設定画面から貼り付けた鍵も、こちらで作って受信機に貼り付ける鍵も同じ形式で扱えるようにするため
// （docs/adr/0040-srtp-external-output.md）。
// 文字列からのパースはこのファイルの parse_srtp_master_key() だけで行い（信頼境界）、
// 内側には検証済みの SrtpMasterKey だけを渡す。
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

#include "vocroz/common/result.h"

namespace vocroz::transport {

/// マスターキーの長さ（バイト）。AES-128 の鍵長。
inline constexpr std::size_t kSrtpMasterKeyBytes = 16;
/// マスターソルトの長さ（バイト）。AES_CM_128_HMAC_SHA1_80/32 の規定値（RFC 3711）。
inline constexpr std::size_t kSrtpMasterSaltBytes = 14;
/// マスターキーとマスターソルトを連結した長さ（バイト）。SDES の inline: に入れる値の長さ。
inline constexpr std::size_t kSrtpKeyMaterialBytes = kSrtpMasterKeyBytes + kSrtpMasterSaltBytes;

/// 鍵を作れなかった原因。
enum class SrtpKeyError : std::uint8_t {
    kMalformed,  ///< Base64 として読めないか、デコードした長さが 30 バイトでない
    /// 鍵の寿命（lifetime）か MKI の指定（"|" で続く部分）がある。1 本のストリームを 1 つの鍵で
    /// 送る運用にするので受け付けない
    kUnsupportedParameter,
    /// すべて 0 の鍵。未設定の入力欄の初期値をそのまま送り出す誤りを防ぐため受け付けない
    kAllZero,
    kRandomUnavailable,  ///< 乱数の取得元（libsodium）を初期化できない
};

/// 検証済みの SRTP のマスターキーとマスターソルト。
/// 秘密情報なので、ログに出さず、破棄とムーブの元で中身を消去する。
/// スレッド安全性: 通常の値型と同じ。
class SrtpMasterKey {
public:
    SrtpMasterKey(const SrtpMasterKey& other) noexcept;
    SrtpMasterKey& operator=(const SrtpMasterKey& other) noexcept;
    SrtpMasterKey(SrtpMasterKey&& other) noexcept;
    SrtpMasterKey& operator=(SrtpMasterKey&& other) noexcept;
    ~SrtpMasterKey();

    /// マスターキーとマスターソルトを連結したバイト列を返す（libsrtp に渡す形）。
    /// @return 30 バイト。このオブジェクトの寿命の間だけ有効
    [[nodiscard]] std::span<const std::uint8_t, kSrtpKeyMaterialBytes> material() const noexcept {
        return material_;
    }

private:
    friend common::Result<SrtpMasterKey, SrtpKeyError> parse_srtp_master_key(
        std::string_view text) noexcept;
    friend common::Result<SrtpMasterKey, SrtpKeyError> generate_srtp_master_key() noexcept;

    SrtpMasterKey() noexcept = default;

    std::array<std::uint8_t, kSrtpKeyMaterialBytes> material_{};
};

/// SDES の inline: と同じ形式の文字列から鍵を作る。
/// 前後の空白と、先頭の "inline:" は省いて読む。
/// @param text 30 バイトを Base64（RFC 4648 の標準のアルファベット）にした 40 文字の文字列
/// @return 検証済みの鍵。Base64 として読めないか長さが違えば kMalformed、"|" で寿命や MKI が
///         続けば kUnsupportedParameter、すべて 0 なら kAllZero
/// スレッド安全性: どのスレッドからも呼び出し可能。
[[nodiscard]] common::Result<SrtpMasterKey, SrtpKeyError> parse_srtp_master_key(
    std::string_view text) noexcept;

/// 鍵を SDES の inline: の形式（"inline:" と 40 文字の Base64）にする。
/// 受信機の設定に貼り付ける文字列として使う。秘密情報なのでログには出さない。
/// @param key 鍵
/// @return "inline:" で始まる文字列
/// スレッド安全性: どのスレッドからも呼び出し可能。ヒープを確保する。
[[nodiscard]] std::string format_srtp_master_key(const SrtpMasterKey& key);

/// 暗号論的な乱数で新しい鍵を作る。
/// @return 新しい鍵。乱数の取得元を初期化できなければ kRandomUnavailable
/// スレッド安全性: どのスレッドからも呼び出し可能。
[[nodiscard]] common::Result<SrtpMasterKey, SrtpKeyError> generate_srtp_master_key() noexcept;

}  // namespace vocroz::transport
