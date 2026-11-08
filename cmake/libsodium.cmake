# SPDX-License-Identifier: AGPL-3.0-only
#
# libsodium を取得し、ターゲット sodium を定義する。使用してよい範囲は AGENTS.md 4.2 の表のとおり。
# libsodium 本体は autotools と Visual Studio のプロジェクトでしかビルドできないため、
# CMake でビルドするラッパー（robinlinden/libsodium-cmake）を使う。ラッパーは libsodium を
# Git のサブモジュールとして固定しているので、ラッパーのコミットで libsodium のバージョン（1.0.20）も決まる。
if(NOT TARGET sodium)
  set(SODIUM_DISABLE_TESTS ON CACHE BOOL "" FORCE)
  set(SODIUM_MINIMAL OFF CACHE BOOL "" FORCE)

  FetchContent_Declare(libsodium
    GIT_REPOSITORY https://github.com/robinlinden/libsodium-cmake.git
    GIT_TAG 9b2848dfc1b917a9410f0de9d81059b26cbfaa8d
    SYSTEM)  # 外部ヘッダーの警告を出さない
  FetchContent_MakeAvailable(libsodium)
endif()
