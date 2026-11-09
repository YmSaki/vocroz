# SPDX-License-Identifier: AGPL-3.0-only
#
# remote-pa: 親機の操作卓の画面のホスト。子機モードのため vocroz::client をリンクする
# （docs/adr/0030-remote-pa-assembly.md）。
# OS に依存しない部品（起動オプション、エンジンへの接続の手順、セッションの稼働の判定、
# ダイアログの文言、子のコマンドライン）は静的ライブラリ remote_pa_core にまとめ、Linux でも単体テストする。
# コンポジションルートと Windows のアダプター（src/win/）は、Windows で JUCE を使うビルドだけに入れる。

set(VOCROZ_RENDEZVOUS_URL "" CACHE STRING "招待 URL の基点にするランデブーサーバーの HTTPS の URL")
configure_file(src/app_config.h.in ${CMAKE_CURRENT_BINARY_DIR}/generated/app_config.h @ONLY)

add_library(remote_pa_core STATIC
  src/command_line.cpp
  src/dialog_text.cpp
  src/engine_attacher.cpp
  src/launch_options.cpp
  src/session_activity_tap.cpp)
target_include_directories(remote_pa_core PUBLIC src ${CMAKE_CURRENT_BINARY_DIR}/generated)
target_link_libraries(remote_pa_core PUBLIC vocroz::control_protocol vocroz::common)
vocroz_set_warnings(remote_pa_core)

vocroz_add_app(remote_pa remote-pa src/main.cpp)
target_link_libraries(remote_pa PRIVATE remote_pa_core vocroz::client)
if(WIN32 AND VOCROZ_WITH_JUCE)
  target_sources(remote_pa PRIVATE
    src/composition_root.cpp
    src/host_mode.cpp
    src/win/console_output.cpp
    src/win/known_paths.cpp
    src/win/win32_dialogs.cpp
    src/win/win32_engine_launcher.cpp
    src/remote-pa.manifest)
  target_compile_definitions(remote_pa PRIVATE
    VOCROZ_REMOTE_PA_HAS_UI WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
  target_link_libraries(remote_pa PRIVATE
    vocroz::ipc
    vocroz::persistence
    vocroz::webui
    vocroz::juce
    shell32
    ole32)
  # ウィンドウのアプリにして、ショートカットから起動したときにコンソールを出さない。
  # エントリポイントは main() のまま（引数のパースを親機モードと子機モードで共有するため）。
  set_target_properties(remote_pa PROPERTIES WIN32_EXECUTABLE TRUE)
  if(MSVC)
    target_link_options(remote_pa PRIVATE /ENTRY:mainCRTStartup)
  endif()
  # 画面（ui/dist）を実行ファイルに埋め込む。configure の前に ui をビルドしておく
  # （cd ui && npm ci && npm run build。docs/adr/0020-ipc-pipe-and-webui-bridge.md）。
  vocroz_embed_ui_assets(remote_pa
    DIRECTORY "${PROJECT_SOURCE_DIR}/ui/dist"
    NAMESPACE vocroz::pa_desktop
    FUNCTION embedded_ui_assets)
  # 親機モードは同じフォルダの pa-engine.exe を起動する。
  add_dependencies(remote_pa pa_engine)
endif()

# 引数の誤りは終了コード 2 で終わる（docs/adr/0007-launch-interface.md）。
add_test(NAME remote_pa.unknown_argument COMMAND remote_pa --no-such-option)
set_tests_properties(remote_pa.unknown_argument PROPERTIES WILL_FAIL TRUE)

if(VOCROZ_BUILD_TESTS)
  vocroz_add_unit_test(remote_pa_tests
    tests/command_line_test.cpp
    tests/dialog_text_test.cpp
    tests/engine_attacher_test.cpp
    tests/launch_options_test.cpp
    tests/session_activity_tap_test.cpp)
  target_link_libraries(remote_pa_tests PRIVATE
    remote_pa_core
    vocroz::common_fakes
    vocroz::control_protocol_fakes)
endif()
