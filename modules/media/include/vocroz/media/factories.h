// SPDX-License-Identifier: AGPL-3.0-only
//
// media のアダプターを作る関数。JUCE を使うので、VOCROZ_WITH_JUCE が ON のときだけ定義がある。
// 具象クラスは公開せず、ここの関数だけを公開する（AGENTS.md 5.1）。
// 形式、ビット深度、空き容量の扱い、レート変換は docs/adr/0043-recording-and-media-player.md。
#pragma once

#include <cstddef>
#include <filesystem>
#include <memory>

#include "vocroz/audio_core/audio_file_error.h"
#include "vocroz/audio_core/audio_file_format.h"
#include "vocroz/audio_core/ports/audio_file_reader.h"
#include "vocroz/audio_core/ports/audio_file_writer.h"
#include "vocroz/common/result.h"

namespace vocroz::media {

/// 録音のファイルを新しく作って開く。48 kHz、24 bit で書く。
/// 書き出しの前にボリュームの空きを調べ、下限（64 MiB）を下回るなら kDiskFull を返して以降は
/// 書かない。WAV は音声の 1 秒ごとにヘッダーを書き直す（異常終了でも直前までを再生できる）。
/// @param file     作るファイル。親フォルダはあること。同じ名前のファイルがあれば上書きしない
/// @param format   形式
/// @param channels チャンネル数（1 か 2）
/// @return 書き出し先。ファイルがすでにあるか作れなければ kIoError、空きが下限を下回っていれば
///         kDiskFull、チャンネル数が範囲外なら kUnsupportedFormat
/// スレッド安全性: どのスレッドからも呼べる（ブロッキング I/O を含む）。返した書き出し先は
/// 1 つのスレッド（ディスク I/O スレッド）からだけ使う。
[[nodiscard]] common::Result<std::unique_ptr<audio_core::AudioFileWriter>,
                             audio_core::AudioFileError>
open_audio_file_writer(const std::filesystem::path& file, audio_core::AudioFileFormat format,
                       std::size_t channels);

/// 再生する音声ファイルを開く。WAV、AIFF、FLAC、Ogg Vorbis を読み、48 kHz に変換して返す。
/// 3 チャンネル以上のファイルは先頭の 2 チャンネルだけを返す。
/// @param file 開くファイル
/// @return 読み込み元。ファイルがなければ kNotFound、読めない形式か壊れていれば
///         kUnsupportedFormat、開けなければ kIoError
/// スレッド安全性: どのスレッドからも呼べる（ブロッキング I/O を含む）。返した読み込み元は
/// 1 つのスレッド（ディスク I/O スレッド）からだけ使う。
[[nodiscard]] common::Result<std::unique_ptr<audio_core::AudioFileReader>,
                             audio_core::AudioFileError>
open_audio_file_reader(const std::filesystem::path& file);

}  // namespace vocroz::media
