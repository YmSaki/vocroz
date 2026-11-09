// SPDX-License-Identifier: AGPL-3.0-only
//
// MIDI デバイスの一覧（MidiDeviceCatalog）が返す、1 つの MIDI 入力ポートの情報。
// オーディオデバイスと同じく、表示名ではなく永続 ID で指定する。同じ機種を 2 台つなぐと
// 表示名が重なり、設定に保存して次回も同じポートを開くには ID が要るため。
#pragma once

#include <string>

namespace vocroz::audio_core {

/// 1 つの MIDI 入力ポートの情報。
struct MidiDeviceInfo {
    std::string id;  ///< 永続 ID（UTF-8）。抜き差しや再起動をまたいで同じポートを指す
    std::string name;  ///< 表示名（UTF-8）。表示だけに使い、ポートの特定には使わない
};

}  // namespace vocroz::audio_core
