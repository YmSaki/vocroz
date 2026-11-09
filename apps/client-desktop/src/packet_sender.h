// SPDX-License-Identifier: AGPL-3.0-only
//
// 子機から親機へのパケットの暗号化と送信。送信方向の PacketSealer は 1 つのスレッドからしか使えず
// （メディアと制御で同じシーケンス番号を使うため）、ソケットへの送信はオーディオスレッドで
// できないので、送信スレッドがメディアと制御の両方を暗号化して送る。
// オーディオスレッドのエンコード済みフレーム、コントロールスレッドの制御パケットの平文と
// 送信先の切り替えは、それぞれ SPSC キューで受け取る（AGENTS.md 6.1）。
#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "vocroz/common/spsc_queue.h"
#include "vocroz/transport/ports/datagram_socket.h"
#include "vocroz/transport/secure_channel.h"

#include "client_constants.h"
#include "encoded_frame.h"
#include "send_wakeup.h"

namespace vocroz::client {

/// 1 つの接続の送信先。接続（ハンドシェイク）ごとに作り、送信スレッドへ所有権ごと渡す。
struct SendPath {
    /// 送信方向の暗号化。
    transport::PacketSealer sealer;
    /// 親機への送信先。
    std::unique_ptr<transport::DatagramSocket> socket;
    /// set_path() が振る世代。これより前に入れた制御パケットは、この送信先では送らない。
    std::uint64_t generation = 0;
};

/// 送信の統計。
struct PacketSenderStats {
    std::uint64_t media_sent = 0;       ///< 送ったメディアパケット
    std::uint64_t control_sent = 0;     ///< 送った制御パケット
    std::uint64_t dropped_no_path = 0;  ///< 送信先がなくて捨てたパケット
    std::uint64_t send_failures = 0;    ///< 暗号化か送信に失敗したパケット
};

// 制御パケットの平文 1 つ。キューに入れるので固定長にする。
struct ControlPlaintext {
    // 入れたときの送信先の世代。信頼制御層は接続ごとに作り直すので、古い接続の平文を
    // 新しい接続の鍵で送ると、相手の新しい信頼制御層が知らない連番を受け取ってしまう。
    std::uint64_t generation = 0;
    std::size_t size = 0;
    std::array<std::uint8_t, transport::kMaxPlaintextBytes> bytes{};
};

/// メディアと制御のパケットを暗号化して送る。
/// 生産者は 3 つ（オーディオスレッドの push_frame()、コントロールスレッドの push_control() と
/// set_path()）、消費者は送信スレッドの pump() だけ。
class PacketSender {
public:
    /// @param wakeup 送るものを入れたときに鳴らす合図。このオブジェクトより長く生存させる
    explicit PacketSender(SendWakeup& wakeup) noexcept;
    ~PacketSender();
    PacketSender(const PacketSender&) = delete;
    PacketSender& operator=(const PacketSender&) = delete;

    /// エンコード済みのフレームを送信待ちに入れる。満杯なら捨てる（受信側からは欠損に見える）。
    /// @param frame フレーム
    /// @return 入れられたら true
    /// スレッド安全性: オーディオスレッド（生産者は 1 つ）から呼び出し可能。
    [[nodiscard]] bool push_frame(const EncodedFrame& frame) noexcept VOCROZ_NONBLOCKING;

    /// 制御パケットの平文を送信待ちに入れる。満杯なら捨てる（信頼制御層が再送する）。
    /// @param plaintext 平文。transport::kMaxPlaintextBytes 以下
    /// @return 入れられたら true。長すぎるか満杯なら false
    /// スレッド安全性: コントロールスレッド（生産者は 1 つ）から呼ぶ。
    [[nodiscard]] bool push_control(std::span<const std::uint8_t> plaintext) noexcept;

    /// 送信先を切り替える。以前の送信先は送信スレッドが破棄する。
    /// @param path 新しい送信先。nullptr なら送信をやめる（以降のパケットは捨てる）
    /// スレッド安全性: コントロールスレッド（生産者は 1 つ）から呼ぶ。
    void set_path(std::unique_ptr<SendPath> path);

    /// 送信待ちのパケットをすべて暗号化して送る。送信先の切り替えを先に反映する。
    /// スレッド安全性: 送信スレッド（消費者は 1 つ）から呼ぶ。ソケットへの送信でブロックしうる。
    void pump() noexcept;

    /// 統計を返す。
    /// スレッド安全性: どのスレッドからも呼び出し可能（値はおおよそ）。
    [[nodiscard]] PacketSenderStats stats() const noexcept;

private:
    // 送信先の切り替えは接続のたびにしか起きないので、数個たまれば足りる。
    static constexpr std::size_t kPathQueueCapacity = 8;

    void apply_path_updates() noexcept;
    void send_sealed(std::size_t bytes, std::atomic<std::uint64_t>& sent_counter) noexcept;

    SendWakeup& wakeup_;
    common::SpscQueue<EncodedFrame, kEncodedFrameQueueCapacity> frames_;
    common::SpscQueue<ControlPlaintext, kControlPlaintextQueueCapacity> control_;
    // 所有権はキューの中では生のポインターで持ち、取り出した側が unique_ptr に戻す。
    common::SpscQueue<SendPath*, kPathQueueCapacity> paths_;
    // 送信スレッドだけが読み書きする。
    std::unique_ptr<SendPath> path_;
    // コントロールスレッドだけが読み書きする。set_path() のたびに 1 進める。
    std::uint64_t control_generation_ = 0;
    std::vector<std::uint8_t> packet_;
    std::atomic<std::uint64_t> media_sent_{0};
    std::atomic<std::uint64_t> control_sent_{0};
    std::atomic<std::uint64_t> dropped_no_path_{0};
    std::atomic<std::uint64_t> send_failures_{0};
};

}  // namespace vocroz::client
