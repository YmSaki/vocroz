// SPDX-License-Identifier: AGPL-3.0-only
//
// 音声グラフのソースノードがサンプルを取り出す先のポート。
// 親機では transport のジッターバッファ（子機ごとの受信ストリーム）が実装する。
// オーディオスレッドから引き出す（pull）形にするのは、デバイスの周期に合わせて
// 必要なぶんだけ取り出し、オーディオスレッドが他のスレッドを待たないようにするため。
// 子機のステレオの入力を 1 本のストリップで扱えるよう、ソースはモノラルかステレオにする
// （docs/adr/0035-client-input-expansion.md）。
#pragma once

#include <cstddef>

#include "vocroz/audio_core/realtime.h"

namespace vocroz::audio_core {

/// モノラルかステレオの音声ソース。
/// ソースは音声グラフ（GraphDescription）に std::shared_ptr で渡す。グラフを差し替えた後、
/// 古いグラフはオーディオスレッド外で破棄されるので、ソースの最後の参照もオーディオスレッド外で外れる。
class AudioSource {
public:
    virtual ~AudioSource() = default;

    /// チャンネル数（1 か 2）を返す。生成後に変えない。範囲外の値はグラフが近い方に丸める。
    /// スレッド安全性: どのスレッドからも呼び出し可能。
    [[nodiscard]] virtual std::size_t channel_count() const noexcept = 0;

    /// 次の frames サンプルを各チャンネルに書き込む。
    /// @param channels channel_count() 本の書き込み先（ステレオは L、R の順）。各 frames サンプル
    /// @param frames   書き込むサンプル数（kBlockSize 以下）。サンプルレートは kSampleRate
    /// エラー時の振る舞い:
    /// 例外を送出しない。サンプルが足りなければ足りないぶんを無音（0）で埋める。
    /// スレッド安全性: オーディオスレッドから呼び出し可能。実装は第 9 節のリアルタイム制約に従う。
    virtual void pull(float* const* channels, std::size_t frames) noexcept VOCROZ_NONBLOCKING = 0;
};

}  // namespace vocroz::audio_core
