# SPDX-License-Identifier: AGPL-3.0-only
#
# libsodium（ISC ライセンス）を取得し、ターゲット sodium を定義する。
# libsodium 本体は autotools と Visual Studio のソリューションでしかビルドできないため、
# ソースに手を加えずに CMake でビルドできるラッパー（robinlinden/libsodium-cmake、ISC）を使う。
# コミットを固定し、libsodium 1.0.20 を取得する。
# 使用してよい範囲は AGENTS.md 4.2 の外部ライブラリの表のとおり（transport、update）。
if(NOT TARGET sodium)
  include(FetchContent)
  set(SODIUM_DISABLE_TESTS ON CACHE BOOL "" FORCE)
  FetchContent_Declare(libsodium
    GIT_REPOSITORY https://github.com/robinlinden/libsodium-cmake.git
    GIT_TAG 9b2848dfc1b917a9410f0de9d81059b26cbfaa8d  # libsodium 1.0.20-FINAL
    GIT_SUBMODULES_RECURSE TRUE
    SYSTEM)  # 外部ヘッダーの警告でビルドを止めない
  FetchContent_MakeAvailable(libsodium)
endif()
