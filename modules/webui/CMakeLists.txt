# SPDX-License-Identifier: AGPL-3.0-only
#
# webui: JUCE の WebBrowserComponent（WebView2）による UI のホストと、UI とのブリッジ（MessageChannel の実装）。
# リソースの検索とブリッジの規則は JUCE に依存しないので、どのプラットフォームでもビルドしてテストする。
# ホストの実装（src/juce/）は VOCROZ_WITH_JUCE が ON のときだけ（docs/adr/0020-ipc-pipe-and-webui-bridge.md）。
add_library(vocroz_webui STATIC
  src/bridge_channel.cpp
  src/ui_resource.cpp)
add_library(vocroz::webui ALIAS vocroz_webui)
target_include_directories(vocroz_webui
  PUBLIC include
  PRIVATE src)
target_link_libraries(vocroz_webui
  PUBLIC vocroz::control_protocol vocroz::common)
if(VOCROZ_WITH_JUCE)
  target_sources(vocroz_webui PRIVATE src/juce/juce_web_ui_host.cpp)
  target_link_libraries(vocroz_webui PRIVATE vocroz::juce)
endif()
vocroz_set_warnings(vocroz_webui)

if(VOCROZ_BUILD_TESTS)
  vocroz_add_unit_test(webui_tests
    tests/bridge_channel_test.cpp
    tests/embedded_assets_test.cpp
    tests/ui_resource_test.cpp)
  # ブリッジは公開しない（ホストの部品）ので、テストは src/ を直接参照する。
  target_include_directories(webui_tests PRIVATE src)
  target_link_libraries(webui_tests PRIVATE vocroz::webui)
  # 埋め込みの関数（cmake/EmbedUiAssets.cmake）を、小さな見本のフォルダで確かめる。
  vocroz_embed_ui_assets(webui_tests
    DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/tests/data/ui-sample"
    NAMESPACE vocroz::webui::test
    FUNCTION sample_ui_assets)
  target_compile_definitions(webui_tests PRIVATE
    VOCROZ_WEBUI_SAMPLE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/data/ui-sample")
endif()
