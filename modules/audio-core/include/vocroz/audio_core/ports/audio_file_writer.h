// SPDX-License-Identifier: AGPL-3.0-only
//
// 録音トラック 1 本ぶんの音声ファイルの書き出し先のポート。
// media が JUCE の AudioFormatWriter（WAV、FLAC）で実装する。ファイルを開く処理（パス、形式、
// ビット深度の指定）は実装側のファクトリー関数が持ち、このポートは開いたファイルへの追記と
// 閉じる処理だけを表す。オーディオスレッドからは呼ばない（RecordingTrack::drain() が
// ディスク I/O スレッドから呼ぶ）。
#pragma once

#include <cstddef>
#include <span>

#include "vocroz/audio_core/audio_file_error.h"
#include "vocroz/common/result.h"

namespace vocroz::audio_core {

/// 開いた音声ファイルへの書き出し。サンプルレートは kSampleRate、チャンネル数は開いたときの値。
class AudioFileWriter {
public:
    virtual ~AudioFileWriter() = default;

    /// インターリーブ形式のサンプルを追記する。
    /// @param interleaved 書き込むサンプル（フレーム数 × チャンネル数）。値は -1〜1 を基準とし、
    /// 範囲外の値の扱い（クリップ）は実装が決める
    /// @return 成功なら値なし。空き容量が足りなければ kDiskFull、それ以外の失敗は kIoError
    /// エラー時の振る舞い: 失敗した呼び出しで書けたサンプルの数は不定。
    /// 以降の呼び出しも失敗してよい。
    /// スレッド安全性: ディスク I/O スレッドから呼ぶ。ブロッキング I/O を含んでよい。
    [[nodiscard]] virtual common::Result<void, AudioFileError> write(
        std::span<const float> interleaved) = 0;

    /// ヘッダーを確定してファイルを閉じる。以降は write() を呼ばない。
    /// @return 成功なら値なし。失敗なら kDiskFull か kIoError
    /// スレッド安全性: ディスク I/O スレッドから呼ぶ。
    [[nodiscard]] virtual common::Result<void, AudioFileError> finish() = 0;
};

}  // namespace vocroz::audio_core
