// SPDX-License-Identifier: AGPL-3.0-only
//
// テスト用の InviteResultListener。受け取った結果を記録する。
// 作業用のスレッドから呼ばれるので、記録はロックで守る。
#pragma once

#include <mutex>
#include <vector>

#include "vocroz/network/ports/invite_result_listener.h"

namespace vocroz::network {

/// 結果を記録する InviteResultListener。
class FakeInviteResultListener final : public InviteResultListener {
public:
    using RegisterResult = common::Result<session::RegisteredInvite, control_protocol::ErrorCode>;
    using ResolveResult = common::Result<session::ResolvedInvite, control_protocol::ErrorCode>;

    struct Registered {
        session::InviteToken token;
        RegisterResult result;
    };
    struct Resolved {
        session::InviteCode code;
        ResolveResult result;
    };

    void on_invite_registered(const session::InviteToken& token,
                              const RegisterResult& result) override {
        const std::lock_guard lock(mutex_);
        registered_.push_back(Registered{token, result});
    }

    void on_invite_resolved(const session::InviteCode& code, const ResolveResult& result) override {
        const std::lock_guard lock(mutex_);
        resolved_.push_back(Resolved{code, result});
    }

    [[nodiscard]] std::vector<Registered> registered() const {
        const std::lock_guard lock(mutex_);
        return registered_;
    }

    [[nodiscard]] std::vector<Resolved> resolved() const {
        const std::lock_guard lock(mutex_);
        return resolved_;
    }

private:
    mutable std::mutex mutex_;
    std::vector<Registered> registered_;
    std::vector<Resolved> resolved_;
};

}  // namespace vocroz::network
