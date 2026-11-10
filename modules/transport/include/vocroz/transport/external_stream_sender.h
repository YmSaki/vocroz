// SPDX-License-Identifier: AGPL-3.0-only
//
// SRTP 外部出力の送信側（送信スレッドの処理）。エンコード済みの Opus のフレームに RTP
// の固定ヘッダーを 付け（RFC 3550、RFC 7587）、SRTP で暗号化して（RFC 3711）、DatagramSocket で送る
// （docs/adr/0040-srtp-external-output.md）。
//
// 平文の禁止: このクラスは検証済みの鍵（SrtpMasterKey）がないと作れず、暗号化に失敗したパケットは
// 送らずに捨てる。暗号化していない RTP を送る経路は持たない。
// 親機と子機の間の独自トランスポート（SecureChannel、PacketHeader）とは部品も鍵も共有しない。
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>

#include "vocroz/common/result.h"
#include "vocroz/transport/constants.h"
#include "vocroz/transport/media_frame.h"
#include "vocroz/transport/ports/datagram_socket.h"
#include "vocroz/transport/ports/random_source.h"
#include "vocroz/transport/srtp_master_key.h"

namespace vocroz::transport {

// libsrtp のセッションを包むクラス（src/srtp_protector.h）。libsrtp
// の型を公開ヘッダーに出さないため、 名前だけを宣言してポインターで持つ。
class SrtpProtector;

/// SRTP の暗号スイート。libsrtp の内蔵の暗号の実装で使えるものだけを持つ。
enum class SrtpCryptoSuite : std::uint8_t {
    /// AES-128 カウンターモードと 80 ビットの HMAC-SHA1。SDES
    /// で実装が必須とされる組み合わせで既定にする
    kAesCm128HmacSha1_80,
    /// AES-128 カウンターモードと 32 ビットの HMAC-SHA1。帯域を詰めたい受信機向け
    kAesCm128HmacSha1_32,
};

/// 動的なペイロードタイプの範囲（RFC 3551）。受信機の SDP の rtpmap と同じ値にする。
inline constexpr std::uint8_t kMinDynamicPayloadType = 96;
/// 動的なペイロードタイプの範囲の上限。
inline constexpr std::uint8_t kMaxDynamicPayloadType = 127;
/// 既定のペイロードタイプ。Opus でよく使われる値。
inline constexpr std::uint8_t kDefaultOpusPayloadType = 111;

/// 外部出力の送信が失敗した原因。
enum class ExternalStreamError : std::uint8_t {
    kInvalidPayloadType,  ///< ペイロードタイプが動的な範囲（96〜127）の外
    kCryptoUnavailable,  ///< libsrtp を初期化できないか、SRTP のセッションを作れない
    kProtectFailed,  ///< SRTP の暗号化に失敗した。そのパケットは送らずに捨てた
    kPayloadTooLarge,  ///< 暗号化したパケットが kMaxPacketBytes に収まらない
    kSendFailed,       ///< ソケットから送れなかった
};

/// 1 本の RTP ストリームの識別と初期値。
/// SSRC と初期値は乱数で決める（make_rtp_stream_params()）。RFC 3550 の推奨で、SRTP の既知平文の
/// 攻撃を難しくするため。
struct RtpStreamParams {
    std::uint8_t payload_type = kDefaultOpusPayloadType;  ///< ペイロードタイプ（96〜127）
    std::uint32_t ssrc = 0;                               ///< 同期送信元の識別子
    std::uint16_t initial_sequence = 0;  ///< 最初のパケットのシーケンス番号
    std::uint32_t initial_timestamp =
        0;  ///< タイムスタンプ 0 のフレームに付ける RTP タイムスタンプ
    FrameDuration frame_duration = FrameDuration::k5000Us;  ///< 1 フレームの長さ
};

/// SSRC と初期値を乱数で決めたストリームの設定を作る。
/// @param random 乱数の取得元。本番では CryptoRandomSource を渡す
/// @param payload_type ペイロードタイプ（96〜127。範囲の検査は ExternalStreamSender::create()
/// が行う）
/// @param frame_duration 1 フレームの長さ（エンコーダーと同じ値）
/// @return 設定
/// スレッド安全性: random の実装に従う。
[[nodiscard]] RtpStreamParams make_rtp_stream_params(RandomSource& random,
                                                     std::uint8_t payload_type,
                                                     FrameDuration frame_duration) noexcept;

/// 1 本の SRTP ストリームの送信。
/// スレッド安全性: create() はコントロールスレッド、send() は送信スレッドから呼ぶ。
/// 1 つのオブジェクトを同時に 1 スレッドからだけ使う。オーディオスレッドからは呼ばない
/// （libsrtp とソケットの呼び出しを含むため）。
class ExternalStreamSender {
public:
    /// SRTP のセッションを作り、送信の準備をする。
    /// @param suite 暗号スイート
    /// @param key 検証済みの鍵。内容は libsrtp のセッションに写し、引数は保持しない
    /// @param params ストリームの識別と初期値
    /// @param socket 宛先が決まっているソケット。このオブジェクトより長く生存させる
    /// @return 送信の準備ができたオブジェクト。ペイロードタイプが範囲外なら kInvalidPayloadType、
    ///         libsrtp がセッションを作れなければ kCryptoUnavailable
    /// スレッド安全性: コントロールスレッドから呼ぶ。ヒープを確保する。
    [[nodiscard]] static common::Result<ExternalStreamSender, ExternalStreamError> create(
        SrtpCryptoSuite suite, const SrtpMasterKey& key, const RtpStreamParams& params,
        DatagramSocket& socket) noexcept;

    ExternalStreamSender(ExternalStreamSender&&) noexcept;
    ExternalStreamSender& operator=(ExternalStreamSender&&) noexcept;
    ExternalStreamSender(const ExternalStreamSender&) = delete;
    ExternalStreamSender& operator=(const ExternalStreamSender&) = delete;
    ~ExternalStreamSender();

    /// 1 フレームを RTP にして暗号化し、送る。
    /// マーカービットは最初のパケットと、フレームのタイムスタンプが 1 フレームより先へ飛んだ後の
    /// 最初のパケットに立てる（送信が途切れた後の再開。RFC 3551 の音声の規則）。
    /// @param frame エンコード済みのフレーム。timestamp は送信開始からの 48kHz のサンプル数
    /// @return 送れたら成功。暗号化に失敗したら kProtectFailed、パケットが長すぎれば
    ///         kPayloadTooLarge、ソケットが失敗したら kSendFailed。どの失敗でも平文は送らず、
    ///         シーケンス番号は進める（同じ番号で別の内容を暗号化しないため）
    /// スレッド安全性: 送信スレッドから呼ぶ。ヒープを確保しない。
    [[nodiscard]] common::Result<void, ExternalStreamError> send(const MediaFrame& frame) noexcept;

    /// 送れたパケットの数を返す。
    [[nodiscard]] std::uint64_t sent_packets() const noexcept {
        return sent_packets_;
    }

    /// 送れずに捨てたパケットの数を返す（暗号化の失敗、長すぎ、ソケットの失敗）。
    [[nodiscard]] std::uint64_t dropped_packets() const noexcept {
        return dropped_packets_;
    }

    /// ストリームの識別と初期値を返す。受信機に知らせる SDP の作成と、テストで使う。
    [[nodiscard]] const RtpStreamParams& params() const noexcept {
        return params_;
    }

private:
    ExternalStreamSender(std::unique_ptr<SrtpProtector> protector, const RtpStreamParams& params,
                         DatagramSocket& socket) noexcept;

    std::unique_ptr<SrtpProtector> protector_;
    RtpStreamParams params_;
    DatagramSocket* socket_;
    std::uint16_t next_sequence_;
    // 直前に送ったフレームのタイムスタンプ。まだ送っていなければ std::nullopt。
    std::optional<std::uint64_t> last_timestamp_;
    std::uint64_t sent_packets_ = 0;
    std::uint64_t dropped_packets_ = 0;
};

}  // namespace vocroz::transport
