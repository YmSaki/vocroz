// SPDX-License-Identifier: AGPL-3.0-only
//
// media の中だけで使う定数（docs/adr/0043-recording-and-media-player.md）。
#pragma once

#include <cstddef>
#include <cstdint>

#include "vocroz/audio_core/constants.h"

namespace vocroz::media {

/// 書き出しと読み込みのチャンネル数の上限。エンジンのトラックはモノラルかステレオなので、
/// それより多いチャンネルは扱わない（読み込みでは先頭の 2 チャンネルだけを使う）。
inline constexpr std::size_t kMaxFileChannels = 2;

/// 録音のビット深度。マイクの録音で 16 bit では余裕が足りず、32 bit float は FLAC で書けないので、
/// WAV と FLAC の両方で 24 bit にそろえる。
inline constexpr int kRecordingBitsPerSample = 24;

/// 24 bit の 1 サンプルのバイト数。空き容量の見積もりに使う。
inline constexpr std::uint64_t kBytesPerRecordedSample = 3;

/// FLAC の圧縮レベル。JUCE の既定（libFLAC の 5）で、ディスク I/O スレッドで 32 トラックを
/// エンコードしても余裕のある速さになる。
inline constexpr int kFlacQualityOptionIndex = 5;

/// 書き出しを止める空き容量の下限（64 MiB）。空きを使い切ると、ヘッダーの書き直しと閉じる処理まで
/// 失敗して録れたぶんも再生できなくなるうえ、OS とほかのアプリの動作に要る空きもなくなるため。
inline constexpr std::uint64_t kMinFreeDiskBytes = 64ULL * 1024 * 1024;

/// 空き容量を調べる間隔（書き出した量、4 MiB）。空きを調べる処理は時間がかかりうるので、
/// 書き出しのたびには調べない。48 kHz、24 bit、ステレオで約 15 秒ぶんで、32 トラックを同時に
/// 書いても下限に達するまでに数回は調べられる。
inline constexpr std::uint64_t kFreeSpaceCheckBytes = 4ULL * 1024 * 1024;

/// WAV のヘッダーを書き直す間隔（音声の 1 秒）。エンジンが異常終了しても、直前の 1 秒までを
/// 再生できるファイルとして残すため。
inline constexpr std::uint64_t kHeaderFlushFrames = audio_core::kSampleRate;

/// 読み込みで 1 回に返す最大のフレーム数。PlaybackTrack が渡す作業領域（kDiskChunkFrames）と
/// 同じ大きさで、レート変換の作業領域をあらかじめ確保しておける。
inline constexpr std::size_t kReadChunkFrames = 4096;

}  // namespace vocroz::media
