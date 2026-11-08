// SPDX-License-Identifier: AGPL-3.0-only
//
// 親機の招待の記録の実装。

#include "invite_book.h"

#include "detail/token_utils.h"

namespace vocroz::session {
namespace {

// 理由を区別して返すために覚えておく、期限切れと取り消しの招待の数。
// 招待は 30 分ごとに発行し直すので、数時間の本番でも収まる件数にする。
// あふれた古い招待は「知らない招待」（INVITE_INVALID）として拒否され、参加できないことは変わらない。
constexpr std::size_t kMaxRetiredInvites = 16;

}  // namespace

void InviteBook::begin_issue(const InviteToken& token) {
    issuing_ = token;
}

bool InviteBook::is_issuing(const InviteToken& token) const noexcept {
    return issuing_ && detail::constant_time_equal(*issuing_, token);
}

void InviteBook::cancel_issue() noexcept {
    issuing_.reset();
}

void InviteBook::complete_issue(const InviteToken& token, const RegisteredInvite& registered,
                                TimePoint expires_at) {
    issuing_.reset();
    if (current_) {
        retire(current_->token, control_protocol::ErrorCode::kInviteRevoked);
    }
    current_ = Active{token, registered, expires_at};
}

bool InviteBook::expire_if_due(TimePoint now) {
    if (!current_ || now < current_->expires_at) {
        return false;
    }
    retire(current_->token, control_protocol::ErrorCode::kInviteExpired);
    current_.reset();
    return true;
}

std::optional<RegisteredInvite> InviteBook::revoke_current() {
    if (!current_) {
        return std::nullopt;
    }
    auto registered = current_->registered;
    retire(current_->token, control_protocol::ErrorCode::kInviteRevoked);
    current_.reset();
    return registered;
}

common::Result<void, control_protocol::ErrorCode> InviteBook::verify(const InviteToken& token,
                                                                     TimePoint now) const {
    if (current_ && detail::constant_time_equal(current_->token, token)) {
        if (now >= current_->expires_at) {
            return common::make_unexpected(control_protocol::ErrorCode::kInviteExpired);
        }
        return {};
    }
    for (const auto& retired : retired_) {
        if (detail::constant_time_equal(retired.token, token)) {
            return common::make_unexpected(retired.reason);
        }
    }
    return common::make_unexpected(control_protocol::ErrorCode::kInviteInvalid);
}

void InviteBook::clear() noexcept {
    issuing_.reset();
    current_.reset();
    retired_.clear();
}

void InviteBook::retire(const InviteToken& token, control_protocol::ErrorCode reason) {
    if (retired_.size() == kMaxRetiredInvites) {
        retired_.pop_front();
    }
    retired_.push_back(Retired{token, reason});
}

}  // namespace vocroz::session
