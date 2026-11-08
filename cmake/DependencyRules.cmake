# SPDX-License-Identifier: AGPL-3.0-only
#
# モジュール間の依存の許可リスト（AGENTS.md 4.2）を configure 時に検査する。
# target_link_libraries 自体はどのターゲットでもリンクできてしまうため、
# 許可リストにない依存を宣言したら configure を失敗させ、依存の向きの違反を早く見つける。
#
# 許可リストを変えるときは、AGENTS.md 4.2 の表と同じ PR でこのファイルを更新する。
# 検査するのは vocroz のモジュール同士の依存だけで、外部ライブラリの使用範囲は検査しない。

# モジュール名（ディレクトリ名の - を _ にしたもの）ごとの、依存してよいモジュール。
set(VOCROZ_ALLOWED_DEPS_common "")
set(VOCROZ_ALLOWED_DEPS_audio_core common)
set(VOCROZ_ALLOWED_DEPS_control_protocol common)
set(VOCROZ_ALLOWED_DEPS_transport audio_core common)
set(VOCROZ_ALLOWED_DEPS_session transport control_protocol audio_core common)
set(VOCROZ_ALLOWED_DEPS_device audio_core common)
set(VOCROZ_ALLOWED_DEPS_capture audio_core common)
set(VOCROZ_ALLOWED_DEPS_network transport session common)
set(VOCROZ_ALLOWED_DEPS_ipc control_protocol common)
set(VOCROZ_ALLOWED_DEPS_webui control_protocol common)
set(VOCROZ_ALLOWED_DEPS_persistence session common)
set(VOCROZ_ALLOWED_DEPS_plugin_host session audio_core common)
set(VOCROZ_ALLOWED_DEPS_media audio_core common)

set(VOCROZ_MODULES
  common audio_core control_protocol transport session
  device capture network ipc webui persistence
  plugin_host media)

# リンク先の名前（vocroz::xxx か vocroz_xxx）からモジュール名を取り出す。
# vocroz のモジュールでなければ空文字列を返す。
function(_vocroz_module_name_of lib out_var)
  set(name "")
  if(lib MATCHES "^vocroz(::|_)(.+)$")
    string(REPLACE "-" "_" candidate "${CMAKE_MATCH_2}")
    if(candidate IN_LIST VOCROZ_MODULES)
      set(name "${candidate}")
    endif()
  endif()
  set(${out_var} "${name}" PARENT_SCOPE)
endfunction()

# 定義済みのすべてのモジュールについて、宣言された依存が許可リストに入っているかを確かめる。
# apps/ はすべてのモジュールに依存してよいので検査しない。
function(vocroz_check_module_dependencies)
  foreach(module IN LISTS VOCROZ_MODULES)
    set(target vocroz_${module})
    if(NOT TARGET ${target})
      continue()
    endif()
    get_target_property(direct ${target} LINK_LIBRARIES)
    get_target_property(interface ${target} INTERFACE_LINK_LIBRARIES)
    set(libs "")
    foreach(list_var IN ITEMS direct interface)
      if(${list_var})
        list(APPEND libs ${${list_var}})
      endif()
    endforeach()
    foreach(lib IN LISTS libs)
      _vocroz_module_name_of("${lib}" dep)
      if(dep STREQUAL "" OR dep STREQUAL module)
        continue()
      endif()
      if(NOT dep IN_LIST VOCROZ_ALLOWED_DEPS_${module})
        message(FATAL_ERROR
          "依存の許可リスト（AGENTS.md 4.2）にない依存です: ${module} -> ${dep}。"
          "依存の向きを AGENTS.md 4.1 の基準で見直すか、表とこのファイルを更新する PR を先に出してください。")
      endif()
    endforeach()
  endforeach()
endfunction()
