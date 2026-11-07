// SPDX-License-Identifier: AGPL-3.0-only
//
// 更新マニフェストとパッケージの取得先のポート。実装は apps/updater/src/win/（WinHTTP）。
#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "vocroz/update/update_manifest.h"

namespace vocroz::update::ports {

/// 更新の取得先。
class UpdateSource {
public:
    virtual ~UpdateSource() = default;

    /// 更新マニフェストを取得する。
    /// @return 取得できたらテキスト。通信の失敗、HTTPS 以外への転送、上限を超える大きさなら
    ///         std::nullopt。コントロールスレッドから呼ぶ（ブロッキング I/O）。
    [[nodiscard]] virtual std::optional<std::string> fetch_manifest() = 0;

    /// マニフェストの URL からパッケージをダウンロードし、ステージング領域に置く。
    /// manifest.package_size を超えて受信したら中止する。
    /// @return ダウンロードできたら true。
    [[nodiscard]] virtual bool download_package(const UpdateManifest& manifest) = 0;

    /// 検証済みのマニフェストを、パッケージと並べてステージング領域に保存する。
    /// --apply はこのマニフェストを読み、もう一度検証してから適用する。
    /// @return 保存できたら true。
    [[nodiscard]] virtual bool store_manifest(std::string_view text) = 0;
};

}  // namespace vocroz::update::ports
