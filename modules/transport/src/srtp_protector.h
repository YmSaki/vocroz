// SPDX-License-Identifier: AGPL-3.0-only
//
// libsrtp の送信側のセッション 1 つを包む。SRTP 外部出力（ExternalStreamSender）だけが使う
// （docs/adr/0040-srtp-external-output.md）。
// libsrtp の NULL 暗号と NULL 認証のポリシーは使わない。鍵は検証済みの SrtpMasterKey
// からだけ受け取る。
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

#include "vocroz/common/result.h"
#include "vocroz/transport/external_stream_sender.h"
#include "vocroz/transport/srtp_master_key.h"

// libsrtp のセッションの型。定義は srtp.h にあり、ここでは名前だけを宣言してポインターとして持つ。
struct srtp_ctx_t_;

namespace vocroz::transport {

/// SRTP のトレイラー（認証タグ）の最大長（バイト）。MKI は使わないので、タグの最大長（libsrtp の
/// SRTP_MAX_TAG_LEN）だけを見込む。暗号化の前のパケットの後ろにこの長さの空きを用意する。
inline constexpr std::size_t kSrtpMaxTrailerBytes = 16;

/// 1 つの SSRC の送信側の SRTP のセッション。
/// スレッド安全性: create() はコントロールスレッド、protect_rtp() は送信スレッドから呼ぶ。
/// 1 つのオブジェクトを同時に 1 スレッドからだけ使う。
class SrtpProtector {
public:
    /// 送信側のセッションを作る。
    /// @param suite 暗号スイート
    /// @param key 検証済みの鍵。libsrtp
    /// がセッションに写す（鍵の導出を済ませる）ので、引数は保持しない
    /// @param ssrc 送る RTP の SSRC
    /// @return セッション。libsrtp を初期化できないか、セッションを作れなければ kCryptoUnavailable
    /// スレッド安全性: どのスレッドからも呼び出し可能。ヒープを確保する。
    [[nodiscard]] static common::Result<SrtpProtector, ExternalStreamError> create(
        SrtpCryptoSuite suite, const SrtpMasterKey& key, std::uint32_t ssrc) noexcept;

    SrtpProtector(SrtpProtector&&) noexcept = default;
    SrtpProtector& operator=(SrtpProtector&&) noexcept = default;
    SrtpProtector(const SrtpProtector&) = delete;
    SrtpProtector& operator=(const SrtpProtector&) = delete;
    ~SrtpProtector() = default;

    /// RTP のパケットをその場で SRTP に暗号化する（ペイロードの暗号化と認証タグの付加）。
    /// @param buffer 先頭 rtp_length バイトが RTP のパケット。後ろに kSrtpMaxTrailerBytes 以上の
    ///               空きがあること。先頭は 4 バイト境界にそろえる（libsrtp がヘッダーを 32 ビット
    ///               単位で読むため）
    /// @param rtp_length RTP のパケットの長さ（バイト）
    /// @return 暗号化した SRTP のパケットの長さ。失敗したら kProtectFailed で、buffer の内容は
    ///         送ってはならない
    /// スレッド安全性: 送信スレッドから呼ぶ。
    [[nodiscard]] common::Result<std::size_t, ExternalStreamError> protect_rtp(
        std::span<std::uint8_t> buffer, std::size_t rtp_length) noexcept;

private:
    // libsrtp のセッションを破棄する。srtp.h をこのヘッダーに含めないよう、定義は .cpp に置く。
    struct Deleter {
        void operator()(srtp_ctx_t_* session) const noexcept;
    };
    using Handle = std::unique_ptr<srtp_ctx_t_, Deleter>;

    explicit SrtpProtector(Handle session) noexcept;

    Handle session_;
};

}  // namespace vocroz::transport
