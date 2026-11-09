// SPDX-License-Identifier: AGPL-3.0-only
//
// RendezvousIdTap の単体テスト。

#include "rendezvous_id_tap.h"

#include <vector>

#include <gtest/gtest.h>

#include "fakes/fake_rendezvous_client.h"

namespace vocroz::engine {
namespace {

TEST(RendezvousIdTapTest, ReportsRendezvousIdBeforeForwardingRegistration) {
    session::FakeRendezvousClient inner;
    std::vector<session::RendezvousId> ids;
    std::size_t registrations_seen_by_handler = 0;
    RendezvousIdTap tap(inner, [&](const session::RendezvousId& id) {
        ids.push_back(id);
        registrations_seen_by_handler = inner.registrations().size();
    });
    session::InviteRegistration registration;
    registration.rendezvous_id.fill(7);
    tap.register_invite(registration);
    ASSERT_EQ(ids.size(), 1U);
    EXPECT_EQ(ids[0], registration.rendezvous_id);
    EXPECT_EQ(registrations_seen_by_handler, 0U);
    ASSERT_EQ(inner.registrations().size(), 1U);
    EXPECT_EQ(inner.registrations()[0], registration);
}

TEST(RendezvousIdTapTest, ForwardsRevokeAndResolve) {
    session::FakeRendezvousClient inner;
    RendezvousIdTap tap(inner, [](const session::RendezvousId&) {});
    const auto code = session::parse_invite_code("ABCDEFGHJK");
    ASSERT_TRUE(code);
    tap.revoke_invite(code.value(), session::RevokeKey{});
    tap.resolve_invite(code.value());
    EXPECT_EQ(inner.revocations().size(), 1U);
    EXPECT_EQ(inner.resolutions().size(), 1U);
}

}  // namespace
}  // namespace vocroz::engine
