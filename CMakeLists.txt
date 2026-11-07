# SPDX-License-Identifier: AGPL-3.0-only
#
# VOCROZ のルート CMake。ビルド全体の設定だけを置き、
# 個々のターゲットは apps/ と modules/ 側で定義する。
cmake_minimum_required(VERSION 3.25)

project(vocroz
  VERSION 0.0.1
  DESCRIPTION "Remote PA console for low-latency ensemble playing over the internet"
  LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(VOCROZ_BUILD_TESTS "単体テスト（GoogleTest）をビルドする" ON)

include(cmake/CompilerWarnings.cmake)
include(cmake/Testing.cmake)
include(cmake/GitHooks.cmake)
include(cmake/DependencyRules.cmake)
include(cmake/libsodium.cmake)

# すべての実行ファイルが共有するバージョン情報（ビルド時に生成）。
configure_file(cmake/version.h.in ${CMAKE_CURRENT_BINARY_DIR}/generated/vocroz/version.h @ONLY)
add_library(vocroz_version INTERFACE)
target_include_directories(vocroz_version INTERFACE ${CMAKE_CURRENT_BINARY_DIR}/generated)

enable_testing()

add_subdirectory(modules)
add_subdirectory(apps)

# すべてのターゲットを定義し終えてから、モジュール間の依存を検査する。
vocroz_check_module_dependencies()
