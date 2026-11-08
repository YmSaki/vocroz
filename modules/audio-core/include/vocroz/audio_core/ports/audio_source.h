// SPDX-License-Identifier: AGPL-3.0-only
//
// 音声グラフのソースノードがサンプルを取り出す先のポート。
// 親機では transport のジッターバッファ（子機ごとの受信ストリーム）が実装する。
// オーディオスレッドから引き出す（pull）形にするのは、デバイスの周期に合わせて
// 必要なぶんだけ取り出し、オーディオスレッドが他のスレッドを待たないようにするため。
#pragma once

#include <span>

#include "vocroz/audio_core/realtime.h"

namespace vocroz::audio_core {

/// モノラルの音声ソース。
/// ソースは音声グラフ（GraphDescription）に std::shared_ptr で渡す。グラフを差し替えた後、
/// 古いグラフはオーディオスレッド外で破棄されるので、ソースの最後の参照もオーディオスレッド外で外れる。
class AudioSource {
public:
    virtual ~AudioSource() = default;

    /// 次の out.size() サンプルを out に書き込む。
    /// @param out 書き込み先（kBlockSize 以下）。サンプルレートは kSampleRate
    /// エラー時の振る舞い:
    /// 例外を送出しない。サンプルが足りなければ足りないぶんを無音（0）で埋める。 スレッド安全性:
    /// オーディオスレッドから呼び出し可能。実装は第 9 節のリアルタイム制約に従う。
    virtual void pull(std::span<float> out) noexcept VOCROZ_NONBLOCKING = 0;
};

}  // namespace vocroz::audio_core
