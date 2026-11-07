# SPDX-License-Identifier: AGPL-3.0-only
#
# プロジェクトのターゲットに厳しめの警告を設定し、警告をエラーとして扱う。
#   vocroz_set_warnings(<target>)
function(vocroz_set_warnings target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /WX /permissive- /utf-8)
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Werror)
  endif()
endfunction()
