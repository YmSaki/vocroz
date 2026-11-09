// SPDX-License-Identifier: AGPL-3.0-only
//
// 1 つの小さなファイルを、一時ファイル → フラッシュ → 名前の置き換えの順でアトミックに保存し、
// 直前の正常な内容を 1 世代のバックアップとして残す。
// どの時点で強制終了しても、本体かバックアップのどちらかに前回か今回の正常な内容が残る。
// 内容が正常かどうか（パースできるか）は知らないので、バックアップに回すかどうかは呼び出し側が決める。
#pragma once

#include <filesystem>
#include <optional>
#include <span>
#include <string>

#include "vocroz/common/result.h"

namespace vocroz::persistence {

/// AtomicFile の読み書きの失敗。
enum class AtomicFileError {
    kIoError,   ///< 読み書きに失敗した
    kTooLarge,  ///< 読み込みの上限より大きい（壊れているとみなす）
};

/// 本体（<path>）、一時ファイル（<path>.tmp）、バックアップ（<path>.bak）の 3 つを扱う。
/// スレッド安全性: 1 つのファイルを複数のスレッドやプロセスから同時に書かない。
class AtomicFile {
public:
    /// @param path 本体のパス。親フォルダは commit() のときになければ作る
    explicit AtomicFile(std::filesystem::path path);

    /// 本体を読む。
    /// @return 内容。本体がなければ std::nullopt
    [[nodiscard]] common::Result<std::optional<std::string>, AtomicFileError> read_current() const;

    /// バックアップを読む。
    /// @return 内容。バックアップがなければ std::nullopt
    [[nodiscard]] common::Result<std::optional<std::string>, AtomicFileError> read_backup() const;

    /// bytes を新しい本体として保存する。
    /// @param bytes                   保存する内容
    /// @param keep_current_as_backup true なら今の本体をバックアップに回す。
    ///        今の本体が壊れているときは false にして、正常なバックアップを壊れた内容で上書きしない
    /// @return 失敗したら理由。失敗しても本体かバックアップに前回の内容が残る
    [[nodiscard]] common::Result<void, AtomicFileError> commit(std::span<const char> bytes,
                                                               bool keep_current_as_backup);

    /// 本体、一時ファイル、バックアップをすべて消す。
    /// @return 失敗したら理由
    [[nodiscard]] common::Result<void, AtomicFileError> remove_all();

    /// 本体のパスを返す。
    [[nodiscard]] const std::filesystem::path& path() const noexcept {
        return path_;
    }

private:
    std::filesystem::path path_;
    std::filesystem::path temporary_path_;
    std::filesystem::path backup_path_;
};

}  // namespace vocroz::persistence
