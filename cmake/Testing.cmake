# SPDX-License-Identifier: AGPL-3.0-only
#
# 単体テストの共通設定。GoogleTest を取得し、vocroz_add_unit_test() を提供する。
# テストはテスト対象モジュールの tests/ に置き、そのモジュールの CMakeLists.txt から登録する。

if(VOCROZ_BUILD_TESTS)
  include(FetchContent)
  FetchContent_Declare(googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SYSTEM)  # 外部ヘッダーの警告でビルドを止めない
  # Windows で親プロジェクトと同じ CRT を使わせる。
  set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
  set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(googletest)
  include(GoogleTest)
endif()

# 単体テスト実行ファイルを作り、CTest に登録する。
#   vocroz_add_unit_test(<target> <source>...)
function(vocroz_add_unit_test target)
  add_executable(${target} ${ARGN})
  target_link_libraries(${target} PRIVATE GTest::gtest_main)
  vocroz_set_warnings(${target})
  gtest_discover_tests(${target})
endfunction()
