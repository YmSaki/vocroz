// SPDX-License-Identifier: AGPL-3.0-only
//
// 再生する音声ファイル 1 つぶんの読み込みとデコードのポート。
// media が JUCE の AudioFormatReader で実装する。ファイルのサンプルレートが kSampleRate と
// 違えば、実装側でレート変換してから返す（device がデバイス境界で変換するのと同じ考え方で、
// エンジン内は 48kHz だけを扱う）。オーディオスレッドからは呼ばない（PlaybackTrack::fill() が
// ディスク I/O スレッドから呼ぶ）。
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "vocroz/audio_core/audio_file_error.h"
#include "vocroz/common/result.h"

namespace vocroz::audio_core {

/// 開いた音声ファイルからの読み込み。
class AudioFileReader {
public:
    virtual ~AudioFileReader() = default;

    /// 返すサンプルのチャンネル数（1 か 2）を返す。3 チャンネル以上のファイルは、実装が
    /// ステレオにまとめるか、開く時点で kUnsupportedFormat にする。
    /// スレッド安全性: どのスレッドからも呼び出し可能（開いた後は変わらない）。
    [[nodiscard]] virtual std::size_t channel_count() const noexcept = 0;

    /// 現在の位置から、インターリーブ形式のサンプルを読む。
    /// @param interleaved 書き込み先（フレーム数 × チャンネル数）
    /// @return 読んだフレーム数。ファイルの終わりなら 0。失敗なら kIoError か kUnsupportedFormat
    /// スレッド安全性: ディスク I/O スレッドから呼ぶ。ブロッキング I/O を含んでよい。
    [[nodiscard]] virtual common::Result<std::size_t, AudioFileError> read(
        std::span<float> interleaved) = 0;

    /// 読む位置を移す。ファイルの長さを超える位置は、ファイルの終わりとして扱う。
    /// @param frame 先頭からのフレーム数（kSampleRate の時計）
    /// @return 成功なら値なし。失敗なら kIoError
    /// スレッド安全性: ディスク I/O スレッドから呼ぶ。
    [[nodiscard]] virtual common::Result<void, AudioFileError> seek(std::uint64_t frame) = 0;
};

}  // namespace vocroz::audio_core
