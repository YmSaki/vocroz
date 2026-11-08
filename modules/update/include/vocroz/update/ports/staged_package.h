// SPDX-License-Identifier: AGPL-3.0-only
//
// ダウンロード済みの更新パッケージ（ZIP）のポート。実装は apps/updater/src/win/。
#pragma once

#include <cstdint>
#include <functional>
#include <span>

#include "vocroz/update/semantic_version.h"

namespace vocroz::update::ports {

/// ダウンロード済みの更新パッケージ。
class StagedPackage {
public:
    /// 読み込んだバイト列を受け取る関数。
    using ChunkSink = std::function<void(std::span<const std::uint8_t>)>;

    virtual ~StagedPackage() = default;

    /// パッケージのバイト列を先頭から順に sink へ渡す。
    /// @return 最後まで読めたら true。読み込みに失敗したら false。コントロールスレッドから呼ぶ。
    [[nodiscard]] virtual bool read(const ChunkSink& sink) = 0;

    /// パッケージを、インストール先のそのバージョンのフォルダに展開する。
    /// 途中で失敗したら、作りかけのフォルダを残さない。
    /// @return 展開できたら true。コントロールスレッドから呼ぶ。
    [[nodiscard]] virtual bool extract(const SemanticVersion& version) = 0;

    /// 検証に失敗したパッケージを削除する。失敗しても次回のダウンロードで上書きされるので、結果は返さない。
    virtual void discard() = 0;
};

}  // namespace vocroz::update::ports
