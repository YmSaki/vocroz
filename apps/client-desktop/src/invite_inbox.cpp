// SPDX-License-Identifier: AGPL-3.0-only
//
// InviteInbox の実装。

#include "invite_inbox.h"

#include <algorithm>
#include <string_view>

namespace vocroz::client {

bool InviteInbox::push(const session::InviteCode& code, const ResolveResult& result) noexcept {
    InviteInboxEntry entry;
    const std::string& normalized = code.normalized();
    std::copy_n(normalized.begin(), std::min(normalized.size(), entry.code.size()),
                entry.code.begin());
    entry.ok = result.has_value();
    if (result) {
        entry.host_public_key = result.value().endpoint.host_public_key;
        entry.rendezvous_id = result.value().endpoint.rendezvous_id;
        entry.invite_token = result.value().invite_token;
    } else {
        entry.error = result.error();
    }
    return queue_.try_push(entry);
}

void InviteInbox::drain(const Handler& handler) {
    while (const auto entry = queue_.try_pop()) {
        const auto code =
            session::parse_invite_code(std::string_view(entry->code.data(), entry->code.size()));
        if (!code) {
            // 自分で入れた正規化済みのコードなので起きないが、起きたら古い要求として捨てる。
            continue;
        }
        if (entry->ok) {
            handler(code.value(), ResolveResult(session::ResolvedInvite{
                                      .endpoint = {.host_public_key = entry->host_public_key,
                                                   .rendezvous_id = entry->rendezvous_id},
                                      .invite_token = entry->invite_token}));
        } else {
            handler(code.value(), common::make_unexpected(entry->error));
        }
    }
}

}  // namespace vocroz::client
