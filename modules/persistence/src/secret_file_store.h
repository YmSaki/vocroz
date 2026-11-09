// SPDX-License-Identifier: AGPL-3.0-only
//
// session の SecretStore を、--data-dir\secrets の下の暗号化したファイルで実装する。
// 暗号化は DataProtector に任せ、ここではファイルの名前とアトミックな保存だけを持つ。
// 秘密情報の保存先を --data-dir の下に置くのは、--data-dir ごとに別の鍵を持たせ
// （同じ PC での親機と子機の検証）、アンインストールの完全削除でフォルダごと消せるようにするため
// （docs/adr/0027-persistence.md）。
#pragma once

#include <filesystem>
#include <memory>

#include "vocroz/session/ports/secret_store.h"

#include "data_protector.h"

namespace vocroz::persistence {

/// 種類ごとに 1 つの暗号化ファイル（<secrets>/<種類>.bin）を持つ。バックアップは作らない。
/// 古い鍵を復元すると、承認済みの子機や招待との対応がかえって崩れるため。
/// スレッド安全性: コントロールスレッドからだけ呼ぶ（SecretStore と同じ）。
class SecretFileStore final : public session::SecretStore {
public:
    /// @param directory 暗号化ファイルを置くフォルダ。なければ最初の保存で作る
    /// @param protector 暗号化と復号。null でないこと
    SecretFileStore(std::filesystem::path directory, std::unique_ptr<DataProtector> protector);

    common::Result<std::vector<std::uint8_t>, session::SecretError> load(
        session::SecretSlot slot) override;
    common::Result<void, session::SecretError> save(session::SecretSlot slot,
                                                    std::span<const std::uint8_t> value) override;
    common::Result<void, session::SecretError> erase(session::SecretSlot slot) override;

private:
    std::filesystem::path directory_;
    std::unique_ptr<DataProtector> protector_;
};

}  // namespace vocroz::persistence
