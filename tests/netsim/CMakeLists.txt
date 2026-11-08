# SPDX-License-Identifier: AGPL-3.0-only
#
# netsim: 結合テストと試験ツールのための障害注入とプロセス内のネットワーク（テスト専用）。
# 製品のターゲットにはリンクしない。障害の判定（FaultInjector）は時刻もソケットも持たない純粋な判定で、
# プロセス内のネットワーク（SimulatedNetwork）と実ソケットの UDP 障害注入プロキシの両方が使う
# （docs/adr/0025-integration-test-harness.md）。
add_library(vocroz_netsim STATIC
  src/fault_injector.cpp
  src/fault_profile.cpp
  src/simulated_network.cpp)
add_library(vocroz::netsim ALIAS vocroz_netsim)
target_include_directories(vocroz_netsim PUBLIC include)
target_link_libraries(vocroz_netsim PUBLIC vocroz::common)
vocroz_set_warnings(vocroz_netsim)

vocroz_add_unit_test(netsim_tests
  tests/fault_injector_test.cpp
  tests/fault_profile_test.cpp
  tests/simulated_network_test.cpp)
target_link_libraries(netsim_tests PRIVATE vocroz::netsim vocroz::common_fakes)
