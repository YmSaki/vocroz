// SPDX-License-Identifier: AGPL-3.0-only
//
// テスト用の RendezvousClient。要求を記録するだけで、結果はテストが session に直接渡す。
#pragma once

#include <utility>
#include <vector>

#include "vocroz/session/ports/rendezvous_client.h"

namespace vocroz::session {

/// 招待 API の要求を記録する RendezvousClient。
class FakeRendezvousClient final : public RendezvousClient {
public:
    void register_invite(const InviteRegistration& registration) override {
        registrations_.push_back(registration);
    }

    void revoke_invite(const InviteCode& code, const RevokeKey& revoke_key) override {
        revocations_.emplace_back(code, revoke_key);
    }

    void resolve_invite(const InviteCode& code) override {
        resolutions_.push_back(code);
    }

    [[nodiscard]] const std::vector<InviteRegistration>& registrations() const noexcept {
        return registrations_;
    }

    [[nodiscard]] const std::vector<std::pair<InviteCode, RevokeKey>>& revocations()
        const noexcept {
        return revocations_;
    }

    [[nodiscard]] const std::vector<InviteCode>& resolutions() const noexcept {
        return resolutions_;
    }

private:
    std::vector<InviteRegistration> registrations_;
    std::vector<std::pair<InviteCode, RevokeKey>> revocations_;
    std::vector<InviteCode> resolutions_;
};

}  // namespace vocroz::session
