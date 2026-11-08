// SPDX-License-Identifier: AGPL-3.0-only
//
// 親機が発行した招待の記録。現在の招待と、期限切れや取り消しになった最近の招待を覚えておき、
// 子機が申請に添えた招待トークンを照合する。
// ランデブーサーバーも期限と取り消しを管理するが、親機自身でも照合する。サーバーが古い招待を
// 解決してしまった場合や、失効の要求が届かなかった場合でも、期限切れ・取り消し済みの招待で
// 参加させないため（認可はエンジンで行う）。
#pragma once

#include <cstddef>
#include <deque>
#include <optional>

#include "vocroz/common/ports/clock.h"
#include "vocroz/common/result.h"
#include "vocroz/control_protocol/states.h"
#include "vocroz/session/identifiers.h"
#include "vocroz/session/ports/rendezvous_client.h"

namespace vocroz::session {

/// 親機の招待の記録。
/// スレッド安全性: コントロールスレッドからだけ使う。
class InviteBook {
public:
    using TimePoint = common::Clock::TimePoint;

    /// 有効な招待。
    struct Active {
        InviteToken token{};
        RegisteredInvite registered;
        TimePoint expires_at{};
    };

    /// 新しい招待の登録を始めたことを記録する。登録が終わるまで、このトークンでは参加できない。
    /// @param token 新しい招待トークン
    void begin_issue(const InviteToken& token);

    /// token が登録中の招待なら true を返す。
    [[nodiscard]] bool is_issuing(const InviteToken& token) const noexcept;

    /// 登録中なら true を返す。
    [[nodiscard]] bool issuing() const noexcept {
        return issuing_.has_value();
    }

    /// 登録の失敗を記録する。
    void cancel_issue() noexcept;

    /// 登録の完了を記録し、現在の招待にする。前の招待があれば取り消し済みにする。
    /// 前提条件: is_issuing(token) が true。
    /// @param token      登録した招待トークン
    /// @param registered ランデブーサーバーの応答
    /// @param expires_at 有効期限
    void complete_issue(const InviteToken& token, const RegisteredInvite& registered,
                        TimePoint expires_at);

    /// 現在の有効な招待を返す。なければ nullptr。
    [[nodiscard]] const Active* current() const noexcept {
        return current_ ? &*current_ : nullptr;
    }

    /// 現在の招待の期限が来ていれば期限切れにする。
    /// @param now 現在時刻
    /// @return 期限切れにしたら true
    [[nodiscard]] bool expire_if_due(TimePoint now);

    /// 現在の招待を取り消し済みにする。
    /// @return 取り消した招待（ランデブーサーバーに失効を伝えるため）。なければ std::nullopt
    [[nodiscard]] std::optional<RegisteredInvite> revoke_current();

    /// 子機が申請に添えた招待トークンを照合する。
    /// @param token 申請の招待トークン
    /// @param now   現在時刻
    /// @return 有効なら成功。期限切れは kInviteExpired、取り消し済みは kInviteRevoked、
    ///         知らないトークンは kInviteInvalid
    [[nodiscard]] common::Result<void, control_protocol::ErrorCode> verify(const InviteToken& token,
                                                                          TimePoint now) const;

    /// すべての記録を消す（セッションの終了）。
    void clear() noexcept;

private:
    struct Retired {
        InviteToken token{};
        control_protocol::ErrorCode reason = control_protocol::ErrorCode::kInviteRevoked;
    };

    void retire(const InviteToken& token, control_protocol::ErrorCode reason);

    std::optional<InviteToken> issuing_;
    std::optional<Active> current_;
    // 期限切れか取り消しになった招待。理由を区別して返すために、最近のものだけを覚えておく。
    std::deque<Retired> retired_;
};

}  // namespace vocroz::session
