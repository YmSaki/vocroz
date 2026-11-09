# SPDX-License-Identifier: AGPL-3.0-only
#
# remote-pa: 親機 UI のホスト。子機モードのため vocroz::client をリンクする。
# 起動オプションのパースは単体テストするため、エントリポイントとは別のライブラリにする。
add_library(remote_pa_launch STATIC src/launch_options.cpp)
target_include_directories(remote_pa_launch PUBLIC src)
target_link_libraries(remote_pa_launch PUBLIC vocroz::common)
vocroz_set_warnings(remote_pa_launch)

vocroz_add_app(remote_pa remote-pa src/main.cpp)
target_link_libraries(remote_pa PRIVATE remote_pa_launch vocroz::client)

# 引数の誤りは終了コード 2 で終わる（docs/adr/0007-launch-interface.md）。
add_test(NAME remote_pa.unknown_argument COMMAND remote_pa --no-such-option)
set_tests_properties(remote_pa.unknown_argument PROPERTIES WILL_FAIL TRUE)

if(VOCROZ_BUILD_TESTS)
  vocroz_add_unit_test(remote_pa_tests tests/launch_options_test.cpp)
  target_link_libraries(remote_pa_tests PRIVATE remote_pa_launch)
endif()
