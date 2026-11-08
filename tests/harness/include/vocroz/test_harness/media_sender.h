// SPDX-License-Identifier: AGPL-3.0-only
//
// 子機の送信側のパイプライン（キャプチャのブロック → フレームへの蓄積 → Opus → 暗号化 → 送信）を
// 結合テスト用に組み立てる。
// 製品では子機ロジック（client-desktop）が担う組み立てで、まだ実装がないため、
// transport の部品を同じ順序でつなぐ最小限の形をここに置く。
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "vocroz/common/result.h"
#include "vocroz/transport/codec_error.h"
#include "vocroz/transport/opus_frame_encoder.h"
#include "vocroz/transport/ports/datagram_socket.h"
#include "vocroz/transport/secure_channel.h"

namespace vocroz::test_harness {

/// 送信側の統計。
struct MediaSenderStats {
    std::uint64_t frames_sent = 0;      ///< 送ったフレーム
    std::uint64_t encode_failures = 0;  ///< エンコードに失敗したフレーム
    std::uint64_t send_failures = 0;    ///< 暗号化か送信に失敗したフレーム
};

/// 1 ストリームの送信側。
/// スレッド安全性: 1 つのスレッドから使う（テストでは仮想時刻を進めるスレッド）。
class MediaSender {
public:
    /// @param sealer        送信方向の暗号化。このオブジェクトより長く生存させる
    /// @param socket        送り先。このオブジェクトより長く生存させる
    /// @param config        エンコーダーの設定
    /// @param stream_id     ストリーム ID
    /// @param mute_epoch    送信ミュートの世代番号（送信を解除した世代）
    /// @return 作った送信側。エンコーダーを作れなければ CodecError
    [[nodiscard]] static common::Result<MediaSender, transport::CodecError> create(
        transport::PacketSealer& sealer, transport::DatagramSocket& socket,
        const transport::OpusEncoderConfig& config, std::uint16_t stream_id,
        std::uint32_t mute_epoch);

    /// キャプチャした 1 ブロックを受け取り、1 フレーム分たまるごとにエンコードして送る。
    /// @param block キャプチャのブロック（長さは任意）
    void capture(std::span<const float> block);

    /// エンコーダーの先読みのサンプル数を返す。遅延の見積もりに使う。
    [[nodiscard]] std::size_t lookahead_samples() const noexcept {
        return encoder_.lookahead_samples();
    }

    /// 1 フレームのサンプル数を返す。
    [[nodiscard]] std::size_t frame_samples() const noexcept {
        return encoder_.frame_samples();
    }

    /// 統計を返す。
    [[nodiscard]] const MediaSenderStats& stats() const noexcept {
        return stats_;
    }

private:
    MediaSender(transport::PacketSealer& sealer, transport::DatagramSocket& socket,
                transport::OpusFrameEncoder encoder, std::uint16_t stream_id,
                std::uint32_t mute_epoch);

    void send_frame();

    transport::PacketSealer* sealer_;
    transport::DatagramSocket* socket_;
    transport::OpusFrameEncoder encoder_;
    std::uint16_t stream_id_;
    std::uint32_t mute_epoch_;
    std::uint64_t next_timestamp_ = 0;
    std::vector<float> pending_;
    std::vector<std::uint8_t> payload_;
    std::vector<std::uint8_t> packet_;
    MediaSenderStats stats_;
};

}  // namespace vocroz::test_harness
