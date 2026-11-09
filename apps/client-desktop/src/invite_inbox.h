// SPDX-License-Identifier: AGPL-3.0-only
//
// 招待コードの解決の結果を、HTTPS の作業用スレッドからコントロールスレッドへ渡す受け箱。
// network の HttpsRendezvousClient は結果を作業用スレッドでリスナーに返すので、この受け箱を
// リスナーとして渡してキューに入れ、ClientRuntime がコントロールスレッドで ClientSession
// へ渡す（AGENTS.md 6.1）。招待コードは std::string を持つので、キューには固定長の文字列で入れ、
// 取り出すときに検証し直して InviteCode に戻す。
#pragma once

#include <array>
#include <cstddef>
#include <functional>
#include <optional>

#include "vocroz/common/result.h"
#include "vocroz/common/spsc_queue.h"
#include "vocroz/control_protocol/states.h"
#include "vocroz/network/ports/invite_result_listener.h"
#include "vocroz/session/invite_code.h"
#include "vocroz/session/ports/rendezvous_client.h"

namespace vocroz::client {

/// InviteInbox のキューに入れる 1 件。std::string を持たない固定長の形にする。
struct InviteInboxEntry {
    std::array<char, session::kInviteCodeLength> code{};
    bool ok = false;
    control_protocol::ErrorCode error = control_protocol::ErrorCode::kUnreachable;
    session::HostPublicKey host_public_key{};
    session::RendezvousId rendezvous_id{};
    session::InviteToken invite_token{};
};

/// 招待の解決の結果の受け箱。
class InviteInbox final : public network::InviteResultListener {
public:
    /// 解決の結果の型（ClientSession::on_invite_resolved() の引数と同じ）。
    using ResolveResult = common::Result<session::ResolvedInvite, control_protocol::ErrorCode>;
    /// 取り出した結果の渡し先。
    using Handler = std::function<void(const session::InviteCode&, const ResolveResult&)>;

    /// 結果を入れる。満杯なら捨てる（出演者は参加をやり直せる。解決の要求は 1 回の参加で 1 つ）。
    /// @param code   解決を要求した招待コード
    /// @param result 解決の結果
    /// @return 入れられたら true
    /// スレッド安全性: 1 つの作業用スレッド（生産者は 1 つ）から呼ぶ。
    [[nodiscard]] bool push(const session::InviteCode& code, const ResolveResult& result) noexcept;

    /// たまった結果をすべて取り出して handler に渡す。
    /// スレッド安全性: コントロールスレッド（消費者は 1 つ）から呼ぶ。
    void drain(const Handler& handler);

    // --- network::InviteResultListener（HttpsRendezvousClient の作業用スレッドから呼ばれる） ---

    /// 子機は招待を登録しないので、届いても捨てる。
    void on_invite_registered(const session::InviteToken& token,
                              const common::Result<session::RegisteredInvite,
                                                   control_protocol::ErrorCode>& result) override;

    /// 解決の結果を push() で入れる。満杯で入らなければ捨てる（push() と同じ理由）。
    void on_invite_resolved(const session::InviteCode& code, const ResolveResult& result) override;

private:
    // 解決の要求は参加の操作 1 回につき 1 つで、同時に数個を超えない。
    static constexpr std::size_t kCapacity = 8;

    common::SpscQueue<InviteInboxEntry, kCapacity> queue_;
};

}  // namespace vocroz::client
