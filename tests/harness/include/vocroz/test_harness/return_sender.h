// SPDX-License-Identifier: AGPL-3.0-only
//
// 親機から子機への返送の送信側（ミックスのブロック → MediaStreamEncoder → PeerPacketSender の
// 暗号化と送信）を、結合テスト用に製品と同じ部品で組み立てる（docs/adr/0036-return-monitor.md）。
// 製品では pa-engine のオーディオスレッドが MediaStreamEncoder にミックスを渡し、接続ごとの
// 送信スレッドが合図で起きて PeerPacketSender を回す。ここではブロックを渡した直後に同じスレッドで
// 回し、送信スレッドが起きるまでの時間は 0 とみなす。
#pragma once

#include <cstdint>
#include <memory>
#include <span>

#include "vocroz/audio_core/realtime.h"
#include "vocroz/common/result.h"
#include "vocroz/transport/codec_error.h"
#include "vocroz/transport/media_stream_encoder.h"
#include "vocroz/transport/opus_frame_encoder.h"
#include "vocroz/transport/peer_packet_sender.h"
#include "vocroz/transport/ports/datagram_socket.h"
#include "vocroz/transport/ports/send_notifier.h"
#include "vocroz/transport/secure_channel.h"

namespace vocroz::test_harness {

/// 1 つの子機への返送の送信側。
/// スレッド安全性: 1 つのスレッドから使う（テストでは仮想時刻を進めるスレッドが、
/// オーディオスレッドと送信スレッドの両方の役をする）。
class ReturnSender {
public:
    /// 返送の解除を済ませた送信側を作る。
    /// @param sealer     送信方向の暗号化。所有権ごと受け取る
    /// @param socket     送り先。このオブジェクトより長く生存させる
    /// @param config     エンコーダーの設定（モノラルにする）
    /// @param mute_epoch 返送のミュートを解除した世代番号
    /// @return 作った送信側。エンコーダーを作れなければ CodecError
    [[nodiscard]] static common::Result<std::unique_ptr<ReturnSender>, transport::CodecError>
    create(transport::PacketSealer sealer, transport::DatagramSocket& socket,
           const transport::OpusEncoderConfig& config, std::uint32_t mute_epoch);

    ReturnSender(const ReturnSender&) = delete;
    ReturnSender& operator=(const ReturnSender&) = delete;
    ReturnSender(ReturnSender&&) = delete;
    ReturnSender& operator=(ReturnSender&&) = delete;
    ~ReturnSender() = default;

    /// ミックスの 1 ブロックを受け取り、溜まったフレームを暗号化して送る。
    /// @param block 親機のオーディオコールバック 1 回分のミックス（モノラル）
    void push(std::span<const float> block);

    /// エンコーダーの先読みのサンプル数を返す。遅延の見積もりに使う。
    [[nodiscard]] std::size_t lookahead_samples() const noexcept {
        return lookahead_samples_;
    }

    /// 送信の統計を返す。
    [[nodiscard]] transport::PeerPacketSenderStats stats() const noexcept {
        return packets_.stats();
    }

    /// create() からだけ呼ぶ。
    ReturnSender(transport::OpusFrameEncoder encoder, transport::PacketSealer sealer,
                 transport::DatagramSocket& socket);

private:
    // 合図を受けたらすぐ送る扱いにするので、合図そのものは使わない。
    class ImmediateNotifier final : public transport::SendNotifier {
    public:
        void notify() noexcept VOCROZ_NONBLOCKING override {}
    };

    ImmediateNotifier notifier_;
    std::size_t lookahead_samples_;
    transport::PeerPacketSender packets_;
    transport::MediaStreamEncoder encoder_;
};

}  // namespace vocroz::test_harness
