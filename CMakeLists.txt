# SPDX-License-Identifier: AGPL-3.0-only
#
# VOCROZ のルート CMake。ビルド全体の設定だけを置き、
# 個々のターゲットは apps/ と modules/ 側で定義する。
cmake_minimum_required(VERSION 3.25)

project(vocroz
  VERSION 0.0.1
  DESCRIPTION "Remote PA console for low-latency ensemble playing over the internet"
  LANGUAGES C CXX)  # C は外部ライブラリ（libopus、libsodium、noise-c）のビルドに使う

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(VOCROZ_BUILD_TESTS "単体テスト（GoogleTest）をビルドする" ON)

# JUCE を使うターゲット（アダプターと apps）をビルドするか。Windows 以外では既定で外す。
# Linux の CI では JUCE が要求する GUI のライブラリを入れずに、ドメインの単体テストだけを回すため。
# JUCE を使うターゲットは if(VOCROZ_WITH_JUCE) の中で定義する。
if(WIN32)
  set(_vocroz_with_juce_default ON)
else()
  set(_vocroz_with_juce_default OFF)
endif()
option(VOCROZ_WITH_JUCE "JUCE を取得し、JUCE を使うターゲットをビルドする" ${_vocroz_with_juce_default})
unset(_vocroz_with_juce_default)

include(cmake/CompilerWarnings.cmake)
include(cmake/Testing.cmake)
include(cmake/GitHooks.cmake)
include(cmake/DependencyRules.cmake)
include(cmake/EmbedUiAssets.cmake)

# 外部ライブラリ（バージョンは各ファイルで固定する。ライセンスは CONTRIBUTING.md）。
include(cmake/ExternalSources.cmake)
include(cmake/opus.cmake)
include(cmake/libsodium.cmake)
include(cmake/noise-c.cmake)
include(cmake/nlohmann_json.cmake)
if(VOCROZ_WITH_JUCE)
  include(cmake/juce.cmake)
endif()

# すべての実行ファイルが共有するバージョン情報（ビルド時に生成）。
configure_file(cmake/version.h.in ${CMAKE_CURRENT_BINARY_DIR}/generated/vocroz/version.h @ONLY)
add_library(vocroz_version INTERFACE)
target_include_directories(vocroz_version INTERFACE ${CMAKE_CURRENT_BINARY_DIR}/generated)

enable_testing()

add_subdirectory(modules)
add_subdirectory(apps)
if(VOCROZ_BUILD_TESTS)
  add_subdirectory(cmake/link-tests)
endif()

# すべてのターゲットを定義し終えてから、モジュール間の依存を検査する。
vocroz_check_module_dependencies()
