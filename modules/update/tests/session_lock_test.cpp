// SPDX-License-Identifier: AGPL-3.0-only
//
// セッション中ミューテックスの名前の単体テスト。

#include "vocroz/update/session_lock.h"

#include <gtest/gtest.h>

namespace vocroz::update {
namespace {

TEST(SessionLockNameTest, IsStableAndLocalToLogonSession) {
    const std::string name = session_lock_name("C:\\Users\\a\\AppData\\Local\\VOCROZ");
    EXPECT_EQ(name.rfind("Local\\VOCROZ.SessionActive.", 0), 0U);
    EXPECT_EQ(name.size(), std::string{"Local\\VOCROZ.SessionActive."}.size() + 16);
    EXPECT_EQ(name, session_lock_name("C:\\Users\\a\\AppData\\Local\\VOCROZ"));
}

TEST(SessionLockNameTest, IgnoresAsciiCaseButSeparatesDataDirs) {
    EXPECT_EQ(session_lock_name("C:\\Data\\VOCROZ"), session_lock_name("c:\\data\\vocroz"));
    EXPECT_NE(session_lock_name("C:\\Data\\VOCROZ"), session_lock_name("C:\\Data\\VOCROZ-test"));
}

}  // namespace
}  // namespace vocroz::update
