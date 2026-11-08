// SPDX-License-Identifier: AGPL-3.0-only
//
// RealtimeSanitizer の検査が有効になっていることの確認。
// CI の RealtimeSanitizer のジョブで、VOCROZ_NONBLOCKING を付けた関数の中のヒープ確保が
// 実際に検出されることを確かめる。検査が効いていなければ、Mixer のテストが違反なしで通っても
// 「違反ゼロ」の根拠にならないため。RealtimeSanitizer を使わないビルドではスキップする。

#include <cstdlib>

#include <gtest/gtest.h>

#include "vocroz/audio_core/realtime.h"

namespace vocroz::audio_core {
namespace {

#if defined(__has_feature)
#if __has_feature(realtime_sanitizer)
#define VOCROZ_TEST_RTSAN_ENABLED 1
#endif
#endif

#if defined(VOCROZ_TEST_RTSAN_ENABLED)
// 最適化で確保が消えないよう、確保したポインターを volatile の変数に書く。
void* volatile g_sink = nullptr;

void allocate_on_audio_thread() noexcept VOCROZ_NONBLOCKING {
    g_sink = std::malloc(16);  // NOLINT(cppcoreguidelines-no-malloc): 違反を起こすためのテスト
}
#endif

TEST(RealtimeSanitizerTest, DetectsHeapAllocationInNonblockingFunction) {
#if defined(VOCROZ_TEST_RTSAN_ENABLED)
    EXPECT_DEATH(allocate_on_audio_thread(), "");
#else
    GTEST_SKIP() << "RealtimeSanitizer を有効にしたビルドでだけ検査する";
#endif
}

}  // namespace
}  // namespace vocroz::audio_core
