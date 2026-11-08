// SPDX-License-Identifier: AGPL-3.0-only
//
// オーディオデバイスの一覧（AudioDeviceCatalog）が返す、1 台のデバイスの情報。
// デバイスは表示名ではなくエンドポイント ID で指定する。表示名は同じ機種を 2 台つなぐと重なり、
// 利用者が名前を変えることもあるため、設定に保存して次回も同じデバイスを開くには ID が要る。
#pragma once

#include <cstddef>
#include <string>

namespace vocroz::audio_core {

/// デバイスの向き。
enum class AudioDeviceDirection {
    kOutput,  ///< 出力（スピーカー、ライン出力）
    kInput,   ///< 入力（マイク、ライン入力）
};

/// 1 台のオーディオデバイスの情報。
struct AudioDeviceInfo {
    std::string id;  ///< エンドポイント ID（UTF-8）。抜き差しや再起動をまたいで同じデバイスを指す
    std::string name;  ///< 表示名（UTF-8）。表示だけに使い、デバイスの特定には使わない
    std::size_t channel_count = 0;  ///< デバイスの共有モードのチャンネル数
    bool is_system_default = false;  ///< OS の既定のデバイスなら true。表示だけに使う
};

}  // namespace vocroz::audio_core
