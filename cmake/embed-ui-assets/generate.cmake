# SPDX-License-Identifier: AGPL-3.0-only
#
# vocroz_embed_ui_assets()（cmake/EmbedUiAssets.cmake）がビルド時に cmake -P で実行するスクリプト。
# DIRECTORY の下の FILES（| 区切りの相対パス）を、UiAsset の配列を返す関数 NAMESPACE::FUNCTION の
# C++ のソースにして OUTPUT に書く。
string(REPLACE "|" ";" files "${FILES}")

set(arrays "")
set(entries "")
set(index 0)
foreach(file IN LISTS files)
  file(READ "${DIRECTORY}/${file}" hex HEX)
  string(LENGTH "${hex}" hex_length)
  math(EXPR size "${hex_length} / 2")
  if(size EQUAL 0)
    # 長さ 0 の配列は書けないので 1 バイトの配列を置き、span の長さで 0 にする。
    set(bytes "0x00")
  else()
    string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${hex}")
  endif()
  string(APPEND arrays "constexpr unsigned char kAsset${index}[] = {${bytes}};\n")
  # パスに " や \ は現れない（Vite の出力名）が、念のためエスケープする。
  string(REPLACE "\\" "\\\\" escaped "${file}")
  string(REPLACE "\"" "\\\"" escaped "${escaped}")
  string(APPEND entries "    {\"${escaped}\", std::span<const unsigned char>{kAsset${index}, ${size}}},\n")
  math(EXPR index "${index} + 1")
endforeach()

if(index EQUAL 0)
  set(table "return {};")
else()
  set(table "static constexpr vocroz::webui::UiAsset kAssets[] = {\n${entries}};\n    return kAssets;")
endif()

set(content "// vocroz_embed_ui_assets() が生成したファイル。編集しない。
#include <span>

#include \"vocroz/webui/ui_assets.h\"

namespace {
${arrays}}  // namespace

namespace ${NAMESPACE} {
vocroz::webui::UiAssetTable ${FUNCTION}();

vocroz::webui::UiAssetTable ${FUNCTION}() {
    ${table}
}
}  // namespace ${NAMESPACE}
")

file(WRITE "${OUTPUT}" "${content}")
