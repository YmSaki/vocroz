# SPDX-License-Identifier: AGPL-3.0-only
#
# 複数のモジュールをまたぐ結合テストと、その試験基盤（docs/adr/0025-integration-test-harness.md）。
# apps/ と同じく、すべてのモジュールに依存してよい。製品のターゲットからはリンクしない。
# GoogleTest を使うので、VOCROZ_BUILD_TESTS が ON のときだけビルドする。
add_subdirectory(netsim)
add_subdirectory(harness)
add_subdirectory(integration)
add_subdirectory(tools)
