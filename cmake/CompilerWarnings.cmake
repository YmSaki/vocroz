# SPDX-License-Identifier: AGPL-3.0-only
#
# プロジェクトのターゲットに厳しめの警告を設定する。
# 警告はビルドを止めない（エラー扱いにしない）。出た警告は直すか、定期リファクタリングで片付ける。
#   vocroz_set_warnings(<target>)
function(vocroz_set_warnings target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8)
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wshadow -Wconversion)
  endif()
endfunction()
