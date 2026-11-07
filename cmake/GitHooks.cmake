# SPDX-License-Identifier: AGPL-3.0-only
#
# 構成時に、このリポジトリの Git フック（.githooks/）を有効にする。
# 開発者が手で設定し忘れても、ビルドした時点でコミット前の自動整形が効くようにするため。
# 設定はこのリポジトリの .git/config にだけ書き、利用者の全体設定は変えない。

find_package(Git QUIET)
if(GIT_FOUND AND EXISTS "${CMAKE_SOURCE_DIR}/.git" AND EXISTS "${CMAKE_SOURCE_DIR}/.githooks")
  execute_process(
    COMMAND "${GIT_EXECUTABLE}" config core.hooksPath .githooks
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    RESULT_VARIABLE vocroz_hooks_result
    OUTPUT_QUIET ERROR_QUIET)
  if(NOT vocroz_hooks_result EQUAL 0)
    message(WARNING "Git フック（.githooks/）を有効にできませんでした。手動で `git config core.hooksPath .githooks` を実行してください。")
  endif()
endif()
