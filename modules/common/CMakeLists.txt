# SPDX-License-Identifier: AGPL-3.0-only
#
# common: どのモジュール・アプリからも使う小さなユーティリティ。
# OS API や他モジュールには依存しない。
add_library(vocroz_common STATIC
  src/cli.cpp
  src/version_info.cpp)
add_library(vocroz::common ALIAS vocroz_common)
target_include_directories(vocroz_common PUBLIC include)
target_link_libraries(vocroz_common PRIVATE vocroz_version)
vocroz_set_warnings(vocroz_common)

if(VOCROZ_BUILD_TESTS)
  vocroz_add_unit_test(common_tests
    tests/cli_test.cpp
    tests/version_info_test.cpp)
  target_link_libraries(common_tests PRIVATE vocroz::common vocroz_version)
endif()
