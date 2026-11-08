# SPDX-License-Identifier: AGPL-3.0-only
#
# 外部ライブラリの取得（cmake/<ライブラリ名>.cmake）が共通で使う関数。
# 外部ライブラリは FetchContent でソースから取得し、バージョンを固定する（docs/adr/0010-external-libraries.md）。

include(FetchContent)

# Git のタグで浅く取得したソースが、期待するコミットであることを確かめる。
# タグは付け替えられることがあるため、タグ名だけでなくコミットのハッシュでも固定する。
# 浅い取得を使うのは、履歴が大きいリポジトリ（JUCE）の取得時間を抑えるため。
#   vocroz_verify_git_revision(<名前> <ソースディレクトリ> <コミットのハッシュ>)
function(vocroz_verify_git_revision name source_dir expected)
  find_package(Git REQUIRED)
  execute_process(
    COMMAND "${GIT_EXECUTABLE}" rev-parse HEAD
    WORKING_DIRECTORY "${source_dir}"
    OUTPUT_VARIABLE actual
    OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE result
    ERROR_QUIET)
  if(NOT result EQUAL 0)
    # FETCHCONTENT_SOURCE_DIR_<名前> で Git 管理外のソースを指定した場合は検査できない。
    message(WARNING "${name} のコミットを確認できませんでした（${source_dir}）。")
  elseif(NOT actual STREQUAL expected)
    message(FATAL_ERROR
      "${name} のコミットが固定した値と違います: ${actual}（期待値 ${expected}）。"
      "タグが付け替えられた可能性があります。")
  endif()
endfunction()
