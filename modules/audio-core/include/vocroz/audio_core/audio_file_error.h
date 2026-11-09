// SPDX-License-Identifier: AGPL-3.0-only
//
// 音声ファイルの書き出しと読み込みが失敗した理由。
// 録音の書き出し（AudioFileWriter）と再生ファイルの読み込み（AudioFileReader）で共通に使い、
// session が理由ごとに録音の停止やエラーの表示を決める。
#pragma once

namespace vocroz::audio_core {

/// 音声ファイルの入出力が失敗した理由。
enum class AudioFileError {
    kDiskFull,  ///< 保存先の空き容量が足りない
    kIoError,  ///< それ以外の書き込みか読み込みの失敗（デバイスの切断、権限など）
    kNotFound,           ///< ファイルが見つからない
    kUnsupportedFormat,  ///< 対応していない形式か、壊れたファイル
};

}  // namespace vocroz::audio_core
