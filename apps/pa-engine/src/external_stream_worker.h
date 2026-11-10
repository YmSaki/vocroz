// SPDX-License-Identifier: AGPL-3.0-only
//
// SRTP 外部出力の送信スレッド（docs/adr/0040-srtp-external-output.md）。
// オーディオスレッドのストリームタップから MediaStreamEncoder がエンコードしたフレームを SPSC
// キューで受け取り、送信スレッドで RTP のヘッダーを付けて SRTP で暗号化して送る。libsrtp と
// ソケットの呼び出しをオーディオスレッドで行わないため（AGENTS.md 第 9 節）。
// 接続テストでは、同じ経路（エンコード、RTP、SRTP、同じソケット）で試験音を実時間の速さで送る。
#pragma once

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <thread>

#include "vocroz/common/spsc_queue.h"
#include "vocroz/control_protocol/states.h"
#include "vocroz/transport/external_stream_sender.h"
#include "vocroz/transport/media_frame.h"
#include "vocroz/transport/media_stream_encoder.h"
#include "vocroz/transport/opus_frame_encoder.h"
#include "vocroz/transport/ports/datagram_socket.h"
#include "vocroz/transport/ports/media_frame_sink.h"
#include "vocroz/transport/send_wakeup.h"

namespace vocroz::engine {

/// オーディオスレッドから送信スレッドへの受け渡し。エンコーダーと、エンコード済みのフレームの
/// キューを一緒に持つ。グラフ（ストリームタップ）はエンコーダーを参照し、エンコーダーはキューを
/// 参照するので、両方を 1 つの共有オブジェクトにまとめ、古いグラフが破棄されるまでキューも残す。
/// スレッド安全性: encoder の push_stereo() はオーディオスレッド（接続テストでは送信スレッド）、
/// フレームの取り出しは送信スレッドだけが行う。
class ExternalStreamPipe final : public transport::MediaFrameSink {
public:
    /// 送信スレッドが止まったときに捨て始めるまでのフレーム数。5ms フレームで 160ms 分。
    static constexpr std::size_t kFrameQueueCapacity = 32;

    /// @param encoder エンコーダー（チャンネル数は送る形式に合わせる）
    /// スレッド安全性: コントロールスレッドで作る。
    explicit ExternalStreamPipe(transport::OpusFrameEncoder encoder);

    ExternalStreamPipe(const ExternalStreamPipe&) = delete;
    ExternalStreamPipe& operator=(const ExternalStreamPipe&) = delete;

    /// エンコード済みのフレームをキューに入れ、送信スレッドを起こす（MediaFrameSink）。
    /// スレッド安全性: オーディオスレッドから呼び出し可能。
    [[nodiscard]] bool push_frame(const transport::MediaFrame& frame) noexcept
        VOCROZ_NONBLOCKING override;

    /// 送り出すバスの音を受け取るエンコーダー。
    [[nodiscard]] transport::MediaStreamEncoder& encoder() noexcept {
        return encoder_;
    }

    /// フレームを 1 つ取り出す。なければ std::nullopt。
    /// スレッド安全性: 送信スレッドだけが呼ぶ。
    [[nodiscard]] std::optional<transport::MediaFrame> pop() noexcept {
        return frames_.try_pop();
    }

    /// 送信スレッドを起こす合図。
    [[nodiscard]] transport::SendWakeup& wakeup() noexcept {
        return wakeup_;
    }

private:
    common::SpscQueue<transport::MediaFrame, kFrameQueueCapacity> frames_;
    transport::SendWakeup wakeup_;
    // frames_ と wakeup_ を参照するので、それらより後に宣言する。
    transport::MediaStreamEncoder encoder_;
};

/// 外部出力の送信スレッド 1 本。
/// スレッド安全性:
/// 生成、start_streaming()、start_test()、stop()、破棄はコントロールスレッドで行う。
class ExternalStreamWorker {
public:
    /// 接続テストの終わりを知らせる関数。送信スレッドから呼ばれる。
    using TestFinished = std::function<void(control_protocol::ExternalOutputResult)>;

    /// @param pipe   エンコーダーとキュー
    /// @param socket 宛先へ送るソケット
    /// @param sender RTP と SRTP の送信。socket を参照していること
    ExternalStreamWorker(std::shared_ptr<ExternalStreamPipe> pipe,
                         std::unique_ptr<transport::DatagramSocket> socket,
                         transport::ExternalStreamSender sender);
    ExternalStreamWorker(const ExternalStreamWorker&) = delete;
    ExternalStreamWorker& operator=(const ExternalStreamWorker&) = delete;
    ~ExternalStreamWorker();

    /// 送信スレッドを起動し、キューに入ったフレームを送り続ける。
    void start_streaming();

    /// 送信スレッドを起動し、試験音（1 kHz、-20 dBFS）を duration の間、実時間の速さで送る。
    /// 終わったら done を送信スレッドから呼ぶ。stop() で止めたときは呼ばない。
    /// @param duration 送る長さ
    /// @param done     結果の渡し先。1 つでも送れなければ kSendFailed、すべて送れたら kOk
    void start_test(std::chrono::milliseconds duration, TestFinished done);

    /// 送信スレッドを止めて待つ。キューに残ったフレームは送らない。2 回目以降は何もしない。
    void stop();

    /// 送ったパケットの数。
    /// スレッド安全性: stop() の後か、送信スレッドが終わった後に読む。
    [[nodiscard]] std::uint64_t sent_packets() const noexcept {
        return sender_.sent_packets();
    }

private:
    void run_streaming() noexcept;
    void run_test(std::chrono::milliseconds duration, const TestFinished& done) noexcept;
    void drain() noexcept;

    std::shared_ptr<ExternalStreamPipe> pipe_;
    std::unique_ptr<transport::DatagramSocket> socket_;
    // socket_ を参照するので、socket_ より後に宣言する（先に破棄する）。
    transport::ExternalStreamSender sender_;
    std::atomic<bool> stopping_{false};
    std::thread thread_;
};

}  // namespace vocroz::engine
