// SPDX-License-Identifier: AGPL-3.0-only
//
// オーディオデバイスのポート（AudioOutput、AudioInput）が返すエラー。
#pragma once

namespace vocroz::audio_core {

/// オーディオデバイスの開始に失敗した原因。
/// UI はこのコードからローカライズ済みのメッセージを表示する（AGENTS.md 6.2）。
enum class AudioDeviceError {
    kDeviceNotFound,     ///< 指定したデバイスが存在しない（抜去済みを含む）
    kDeviceBusy,         ///< 他のアプリケーションが使用中で開けない
    kUnsupportedFormat,  ///< 内部フォーマット（48kHz Float32）に変換できない
    kAlreadyStarted,     ///< すでに開始している
    kInternal,           ///< OS や外部ライブラリの想定外のエラー
};

}  // namespace vocroz::audio_core
