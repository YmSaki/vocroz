# SPDX-License-Identifier: AGPL-3.0-only
#
# UI のビルド成果物（ui/dist）を、webui の UiAssetTable の形で実行ファイルに埋め込む。
# ファイルの一覧と内容を C++ の配列にしたソースをビルド時に生成し、ターゲットに加える
# （docs/adr/0020-ipc-pipe-and-webui-bridge.md）。
#   vocroz_embed_ui_assets(<target>
#     DIRECTORY <埋め込むフォルダ>
#     NAMESPACE <生成する関数の名前空間（例: vocroz::app）>
#     FUNCTION  <生成する関数の名前（例: embedded_ui_assets）>)
# 生成した関数は `vocroz::webui::UiAssetTable <NAMESPACE>::<FUNCTION>()` で、呼び出す側で同じ宣言を書く。
# ファイルの一覧は configure 時に決まる（CONFIGURE_DEPENDS で、追加や削除も次のビルドで反映される）。
function(vocroz_embed_ui_assets target)
  cmake_parse_arguments(ARG "" "DIRECTORY;NAMESPACE;FUNCTION" "" ${ARGN})
  if(NOT ARG_DIRECTORY OR NOT ARG_NAMESPACE OR NOT ARG_FUNCTION)
    message(FATAL_ERROR "vocroz_embed_ui_assets: DIRECTORY、NAMESPACE、FUNCTION を指定してください")
  endif()
  if(NOT IS_DIRECTORY "${ARG_DIRECTORY}")
    message(FATAL_ERROR "vocroz_embed_ui_assets: ${ARG_DIRECTORY} がありません。先に ui をビルドしてください（cd ui && npm ci && npm run build）")
  endif()

  file(GLOB_RECURSE files CONFIGURE_DEPENDS LIST_DIRECTORIES false
    RELATIVE "${ARG_DIRECTORY}" "${ARG_DIRECTORY}/*")
  list(SORT files)
  set(output "${CMAKE_CURRENT_BINARY_DIR}/${target}_ui_assets.cpp")
  set(inputs "")
  foreach(file IN LISTS files)
    list(APPEND inputs "${ARG_DIRECTORY}/${file}")
  endforeach()
  # 一覧はセミコロン区切りのままスクリプトに渡すと分割されるので、| でつなぐ。
  string(REPLACE ";" "|" file_list "${files}")

  add_custom_command(
    OUTPUT "${output}"
    COMMAND "${CMAKE_COMMAND}"
      "-DDIRECTORY=${ARG_DIRECTORY}"
      "-DFILES=${file_list}"
      "-DNAMESPACE=${ARG_NAMESPACE}"
      "-DFUNCTION=${ARG_FUNCTION}"
      "-DOUTPUT=${output}"
      -P "${VOCROZ_EMBED_UI_ASSETS_SCRIPT}"
    DEPENDS ${inputs} "${VOCROZ_EMBED_UI_ASSETS_SCRIPT}"
    COMMENT "UI のファイルを埋め込むソースを生成しています（${target}）"
    VERBATIM)
  target_sources(${target} PRIVATE "${output}")
  target_link_libraries(${target} PRIVATE vocroz::webui)
endfunction()

set(VOCROZ_EMBED_UI_ASSETS_SCRIPT "${CMAKE_CURRENT_LIST_DIR}/embed-ui-assets/generate.cmake")
