// SPDX-License-Identifier: AGPL-3.0-only
//
// 親機の受信側のパイプライン（受信 → 復号とリプレイ排除 → 信号状態の判定 →
// ジッターバッファと再生）を 結合テスト用に組み立てる。 製品では pa-engine
// が組み立てるもので、まだ実装がないため、transport の部品を同じ順序でつなぐ
// 最小限の形をここに置く。送信ミュートの通知は制御チャンネルの代わりに unmute() で直接渡す。
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "vocroz/common/ports/clock.h"
#include "vocroz/common/result.h"
#include "vocroz/transport/media_payload.h"
#include "vocroz/transport/playout_stream.h"
#include "vocroz/transport/secure_channel.h"
#include "vocroz/transport/signal_monitor.h"
#include "vocroz/transport/signal_state.h"

namespace vocroz::test_harness {

/// 受信側の統計。
struct MediaReceiverStats {
    std::uint64_t datagrams = 0;     ///< 受け取ったデータグラム
    std::uint64_t media_frames = 0;  ///< 復号して再生側に渡したメディアフレーム
    std::uint64_t rejected_replay = 0;  ///< リプレイ（重複）として捨てたパケット
    std::uint64_t rejected_invalid = 0;  ///< 認証の失敗など、リプレイ以外の理由で捨てたパケット
    std::uint64_t rejected_by_signal =
        0;  ///< 信号状態の判定で捨てたフレーム（ミュート中、古い世代）
};

/// 1 ストリームの受信側。
/// スレッド安全性: 1 つのスレッドから使う（テストでは仮想時刻を進めるスレッドが、
/// ネットワーク受信スレッドとオーディオスレッドの両方の役をする）。
class MediaReceiver {
public:
    /// @param opener 受信方向の復号。このオブジェクトより長く生存させる
    /// @param config 再生の設定
    /// @param now    今の時刻（信号状態の起点）
    /// @return 作った受信側。再生ストリームを作れなければ PlayoutError
    [[nodiscard]] static common::Result<std::unique_ptr<MediaReceiver>, transport::PlayoutError>
    create(transport::PacketOpener& opener, const transport::PlayoutConfig& config,
           common::Clock::TimePoint now);

    /// 送信ミュートの解除を受け取ったものとする（制御チャンネルの代わり）。
    /// @param mute_epoch 解除した世代番号
    /// @param now        今の時刻
    void unmute(std::uint32_t mute_epoch, common::Clock::TimePoint now);

    /// 送信ミュートの通知を受け取る（親機の PeerLink::apply_send_mute() から）。
    /// @param send_muted 送信ミュート中なら true
    /// @param mute_epoch 通知の世代番号
    /// @param now        今の時刻
    void on_send_mute(bool send_muted, std::uint32_t mute_epoch, common::Clock::TimePoint now);

    /// 復号済みのメディアの中身を処理する（復号と振り分けを呼び出し側が済ませた場合）。
    /// @param media 復号したメディアの中身
    /// @param now   今の時刻
    void on_media(const transport::MediaPayload& media, common::Clock::TimePoint now);

    /// 受信したデータグラムを処理する。
    /// @param datagram 受信したデータグラム
    /// @param now      今の時刻
    void on_datagram(std::span<const std::uint8_t> datagram, common::Clock::TimePoint now);

    /// 時間の経過で信号状態を更新する。
    /// @param now 今の時刻
    void poll(common::Clock::TimePoint now);

    /// 再生する PCM を書き込む（オーディオコールバックの代わり）。
    /// @param out 書き込み先
    void render(std::span<float> out);

    /// 信号状態を返す。
    [[nodiscard]] transport::SignalState signal_state() const noexcept {
        return monitor_.state();
    }

    /// 再生ストリームの統計を返す。
    [[nodiscard]] transport::PlayoutStats playout_stats() const noexcept {
        return playout_->stats();
    }

    /// 受信側の統計を返す。
    [[nodiscard]] const MediaReceiverStats& stats() const noexcept {
        return stats_;
    }

    /// create() からだけ呼ぶ。
    MediaReceiver(transport::PacketOpener& opener,
                  std::unique_ptr<transport::PlayoutStream> playout, common::Clock::TimePoint now);

private:
    transport::PacketOpener& opener_;
    std::unique_ptr<transport::PlayoutStream> playout_;
    transport::SignalMonitor monitor_;
    std::vector<std::uint8_t> plaintext_;
    MediaReceiverStats stats_;
};

}  // namespace vocroz::test_harness
