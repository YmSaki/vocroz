# SPDX-License-Identifier: AGPL-3.0-only
#
# common: どのモジュール・アプリからも使う共通基盤。
# ユーティリティ、Result 型、SPSC キュー、横断的関心事のポート（Clock、Logger）を持つ。
# OS API や他モジュールには依存しない。
add_library(vocroz_common STATIC
  src/cli.cpp
  src/version_info.cpp)
add_library(vocroz::common ALIAS vocroz_common)
target_include_directories(vocroz_common PUBLIC include)
target_link_libraries(vocroz_common PRIVATE vocroz_version)
vocroz_set_warnings(vocroz_common)

if(VOCROZ_BUILD_TESTS)
  # Clock と Logger のフェイク。他モジュールのテストからも
  # #include "fakes/fake_clock.h" の形で使えるよう、ヘッダーだけのターゲットとして公開する。
  add_library(vocroz_common_fakes INTERFACE)
  add_library(vocroz::common_fakes ALIAS vocroz_common_fakes)
  target_include_directories(vocroz_common_fakes INTERFACE tests)
  target_link_libraries(vocroz_common_fakes INTERFACE vocroz::common)

  vocroz_add_unit_test(common_tests
    tests/cli_test.cpp
    tests/fakes_test.cpp
    tests/result_test.cpp
    tests/spsc_queue_test.cpp
    tests/version_info_test.cpp)
  target_link_libraries(common_tests PRIVATE vocroz::common vocroz::common_fakes vocroz_version)
endif()
