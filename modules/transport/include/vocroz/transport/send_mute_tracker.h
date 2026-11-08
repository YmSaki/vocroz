// SPDX-License-Identifier: AGPL-3.0-only
//
// 子機の送信ミュートの状態を、ミュートの世代番号（epoch）で追跡する。
// 制御メッセージ（信頼性あり）とメディアフレーム（信頼性なし）は別の経路で届くので、
// 到着順が逆転しうる。
// 世代番号を比べて、古い通知や、ミュート前に送られた古いフレームを捨てる。
//
// 世代番号の規則: 子機は送信ミュートを解除するたびに世代番号を 1 増やし、
// その世代番号をフレームに載せて送る。
// ミュートの通知と解除の通知も (send_muted, 世代番号) を運ぶ。
// 同じ世代では「解除 → ミュート」の順に起きるので、
// (世代番号, ミュート中か) の組の辞書順で新しさを比べられる。
#pragma once

#include <cstdint>

namespace vocroz::transport {

/// 通知を適用した結果。
enum class MuteUpdate : std::uint8_t {
    kIgnored,  ///< 既知の状態より古いか同じ通知だった
    kMuted,    ///< 送信ミュートになった
    kUnmuted,  ///< 新しい世代で送信が再開された
};

/// フレームの扱い。
enum class FrameVerdict : std::uint8_t {
    kAccept,            ///< 現在の区間のフレーム
    kAcceptNewSegment,  ///< 解除の通知より先に届いた、新しい世代のフレーム。解除を知ったものとして受け付ける
    kDrop,  ///< ミュート前の古い世代か、ミュート中の世代のフレーム。破棄する
};

/// 送信ミュートの状態。
/// 初期状態は送信ミュート（世代番号 0）で、起動時と新規参加時のミュートの規則に合わせる。
/// 世代番号の桁あふれは考えない（1 秒に 1 回解除しても 100 年以上かかる）。
/// スレッド安全性: 1 つのオブジェクトを同時に 1 スレッドからだけ使う。
class SendMuteTracker {
public:
    /// ミュートか解除の通知を適用する。
    /// @param send_muted 通知されたミュート状態
    /// @param epoch      通知に載っていた世代番号
    /// @return 状態が変わったかどうか
    /// スレッド安全性: オーディオスレッドから呼び出し可能（ヒープを確保せず、ブロックしない）。
    [[nodiscard]] MuteUpdate apply(bool send_muted, std::uint32_t epoch) noexcept;

    /// フレームの扱いを決める。
    /// kAcceptNewSegment のときは、その世代で解除されたものとして状態を進める。
    /// @param epoch フレームに載っていた世代番号
    /// @return フレームの扱い
    /// スレッド安全性: オーディオスレッドから呼び出し可能。
    [[nodiscard]] FrameVerdict classify(std::uint32_t epoch) noexcept;

    /// 送信ミュート中なら true を返す。
    [[nodiscard]] bool send_muted() const noexcept {
        return muted_;
    }

    /// 現在の世代番号を返す。
    [[nodiscard]] std::uint32_t epoch() const noexcept {
        return epoch_;
    }

private:
    std::uint32_t epoch_ = 0;
    bool muted_ = true;
};

}  // namespace vocroz::transport
