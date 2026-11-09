// SPDX-License-Identifier: AGPL-3.0-only
//
// 秘密情報（長期鍵）の読み込みと保存のポート。
// persistence が DPAPI で暗号化したファイルとして実装する（docs/adr/0027-persistence.md）。
// 秘密情報は設定ファイルやログに平文で書かない製品要件があるため、設定（SettingsStore）とは
// 別のポートにして、保存先と保護の方法を設定から切り離す。
#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "vocroz/common/result.h"

namespace vocroz::session {

/// 保存する秘密情報の種類。種類ごとに 1 つの値を持つ。
enum class SecretSlot {
    /// 親機の Noise の静的秘密鍵。招待と再接続で同じ親機と確かめるために使い続ける。
    kHostStaticKey,
    /// 子機の Noise の静的秘密鍵。
    kClientStaticKey,
};

/// 秘密情報を読み書きできなかった理由。
enum class SecretError {
    /// 保存された値がない（初回の起動）。
    kNotFound,
    /// 値が壊れているか、別のユーザーや PC で保護されていて復号できない。
    kCorrupted,
    /// 読み書きに失敗した。
    kIoError,
};

/// 秘密情報の保存先。値は保存先で保護し（Windows では DPAPI）、平文をディスクに残さない。
/// 呼び出し側は、受け取った平文を使い終えたらゼロで上書きする。
/// スレッド安全性: コントロールスレッドからだけ呼ぶ。実装はファイル I/O でブロックしてよい。
class SecretStore {
public:
    virtual ~SecretStore() = default;

    /// 秘密情報を読む。
    /// @param slot 種類
    /// @return 平文。読めなければ理由。kCorrupted なら呼び出し側は値を作り直して保存し直す
    [[nodiscard]] virtual common::Result<std::vector<std::uint8_t>, SecretError> load(
        SecretSlot slot) = 0;

    /// 秘密情報を保存する。途中で強制終了しても、前回の値か今回の値のどちらかが残る。
    /// @param slot  種類
    /// @param value 平文。呼び出しから戻った後は参照されない
    /// @return 失敗したら理由
    [[nodiscard]] virtual common::Result<void, SecretError> save(
        SecretSlot slot, std::span<const std::uint8_t> value) = 0;

    /// 秘密情報を消す。保存されていなければ何もせず成功する。
    /// @param slot 種類
    /// @return 失敗したら理由
    [[nodiscard]] virtual common::Result<void, SecretError> erase(SecretSlot slot) = 0;
};

}  // namespace vocroz::session
