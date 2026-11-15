// SPDX-License-Identifier: AGPL-3.0-only
//
// 録音で書き出す音声ファイルの形式。session が録音の設定から選び、media がこの形式で
// ファイルを開く（docs/adr/0043-recording-and-media-player.md）。
#pragma once

namespace vocroz::audio_core {

/// 録音で書き出すファイルの形式。どちらも 48 kHz、24 bit で書く。
enum class AudioFileFormat {
    kWav,   ///< WAV（4 GiB を超えたら RF64）
    kFlac,  ///< FLAC（可逆圧縮）
};

}  // namespace vocroz::audio_core
