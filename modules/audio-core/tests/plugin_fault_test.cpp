// SPDX-License-Identifier: AGPL-3.0-only
//
// PluginFault の単体テスト。印は立てたら下ろさないことを検証する。

#include "vocroz/audio_core/plugin_fault.h"

#include <gtest/gtest.h>

namespace vocroz::audio_core {
namespace {

TEST(PluginFaultTest, StartsCleared) {
    const PluginFault fault;
    EXPECT_FALSE(fault.raised());
}

TEST(PluginFaultTest, StaysRaisedOnceRaised) {
    PluginFault fault;
    fault.raise();
    fault.raise();
    EXPECT_TRUE(fault.raised());
}

}  // namespace
}  // namespace vocroz::audio_core
