// SPDX-License-Identifier: AGPL-3.0-only
//
// SrtpProtector の実装。libsrtp の呼び出しはこのファイルだけに置く。

#include "srtp_protector.h"

#include <climits>
#include <cstring>
#include <utility>

#include <srtp.h>

namespace vocroz::transport {
namespace {

static_assert(SRTP_AES_ICM_128_KEY_LEN_WSALT == kSrtpKeyMaterialBytes);
static_assert(SRTP_MAX_TAG_LEN == kSrtpMaxTrailerBytes);

// リプレイ排除の窓（libsrtp の既定の最小値）。送信側では使わないが、ポリシーの必須の値。
constexpr unsigned long kReplayWindowSize = 128;

// libsrtp の初期化は 1 回だけ行う。srtp_init() はスレッド安全でないので、関数内の static の
// 初期化で 1 回にする。srtp_shutdown() は呼ばない（プロセスの終了まで使う）。
bool ensure_srtp_initialized() noexcept {
    static const bool initialized = srtp_init() == srtp_err_status_ok;
    return initialized;
}

void set_crypto_policy(SrtpCryptoSuite suite, srtp_crypto_policy_t& policy) noexcept {
    switch (suite) {
        case SrtpCryptoSuite::kAesCm128HmacSha1_80:
            srtp_crypto_policy_set_aes_cm_128_hmac_sha1_80(&policy);
            return;
        case SrtpCryptoSuite::kAesCm128HmacSha1_32:
            srtp_crypto_policy_set_aes_cm_128_hmac_sha1_32(&policy);
            return;
    }
    // 列挙子以外の値は来ないが、来ても認証タグの長い既定のスイートにする。
    srtp_crypto_policy_set_aes_cm_128_hmac_sha1_80(&policy);
}

}  // namespace

common::Result<SrtpProtector, ExternalStreamError> SrtpProtector::create(
    SrtpCryptoSuite suite, const SrtpMasterKey& key, std::uint32_t ssrc) noexcept {
    if (!ensure_srtp_initialized()) {
        return common::make_unexpected(ExternalStreamError::kCryptoUnavailable);
    }
    // libsrtp は鍵を書き換えないが、ポリシーの型が非 const のポインターなので一時的に写す。
    SrtpMasterKey material = key;
    srtp_policy_t policy;
    std::memset(&policy, 0, sizeof(policy));
    set_crypto_policy(suite, policy.rtp);
    // SRTCP の送受信はまだ行わないが、受信レポートの確認を足すときに同じ強さにそろえておく。
    srtp_crypto_policy_set_aes_cm_128_hmac_sha1_80(&policy.rtcp);
    policy.ssrc.type = ssrc_specific;
    policy.ssrc.value = ssrc;
    policy.key = const_cast<unsigned char*>(material.material().data());
    policy.window_size = kReplayWindowSize;
    policy.allow_repeat_tx = 0;  // 同じシーケンス番号での再送を禁じ、鍵ストリームの再利用を防ぐ
    srtp_t session = nullptr;
    if (srtp_create(&session, &policy) != srtp_err_status_ok) {
        return common::make_unexpected(ExternalStreamError::kCryptoUnavailable);
    }
    return SrtpProtector(Handle(session));
}

void SrtpProtector::Deleter::operator()(srtp_ctx_t_* session) const noexcept {
    // srtp_dealloc() は鍵の導出結果を含むセッションのメモリを消去してから解放する。
    srtp_dealloc(session);
}

SrtpProtector::SrtpProtector(Handle session) noexcept : session_(std::move(session)) {}

common::Result<std::size_t, ExternalStreamError> SrtpProtector::protect_rtp(
    std::span<std::uint8_t> buffer, std::size_t rtp_length) noexcept {
    if (rtp_length > buffer.size() || buffer.size() - rtp_length < kSrtpMaxTrailerBytes ||
        buffer.size() > static_cast<std::size_t>(INT_MAX)) {
        return common::make_unexpected(ExternalStreamError::kProtectFailed);
    }
    int length = static_cast<int>(rtp_length);
    if (srtp_protect(session_.get(), buffer.data(), &length) != srtp_err_status_ok || length < 0) {
        return common::make_unexpected(ExternalStreamError::kProtectFailed);
    }
    return static_cast<std::size_t>(length);
}

}  // namespace vocroz::transport
