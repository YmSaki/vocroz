# SPDX-License-Identifier: AGPL-3.0-only
#
# 子機モード。単独の実行ファイルではなく remote-pa に組み込み、--mode=client（--client）か招待で起動する。
# OS に依存しない組み立て（ClientRuntime とその部品）はどのプラットフォームでもビルドして単体テストし、
# WebView、WASAPI、Winsock をつなぐコンポジションルートは Windows で JUCE を使うときだけビルドする
# （docs/adr/0030-remote-pa-assembly.md）。
add_library(vocroz_client STATIC
  src/capture_processor.cpp
  src/client_app.cpp
  src/client_capture.cpp
  src/client_runtime.cpp
  src/host_connection.cpp
  src/invite_inbox.cpp
  src/packet_sender.cpp
  src/peer_inbox.cpp
  src/sender_thread.cpp
  src/server_url.cpp
  src/ui_bridge.cpp)
add_library(vocroz::client ALIAS vocroz_client)
target_include_directories(vocroz_client
  PUBLIC include
  PRIVATE src)
target_link_libraries(vocroz_client
  PUBLIC vocroz::common
  PRIVATE
    vocroz::audio_core
    vocroz::control_protocol
    vocroz::network
    vocroz::session
    vocroz::transport)
vocroz_set_warnings(vocroz_client)

if(VOCROZ_BUILD_TESTS)
  vocroz_add_unit_test(client_tests
    tests/capture_processor_test.cpp
    tests/client_capture_test.cpp
    tests/client_runtime_test.cpp
    tests/host_connection_test.cpp
    tests/invite_inbox_test.cpp
    tests/packet_sender_test.cpp
    tests/peer_inbox_test.cpp
    tests/server_url_test.cpp
    tests/ui_bridge_test.cpp)
  # src/ のクラスは公開せず、このアプリのテストからだけ参照する。
  # ランデブーサーバーの REGISTERED と PUNCH を組み立てるヘルパーは network のテストのものを使う
  # （サーバー側のシリアライザーは C++ にないため。tests/ を参照するのはテストのターゲットだけ）。
  target_include_directories(client_tests PRIVATE
    src
    tests
    ${PROJECT_SOURCE_DIR}/modules/network/src
    ${PROJECT_SOURCE_DIR}/modules/network/tests)
  target_link_libraries(client_tests PRIVATE
    vocroz::client
    vocroz::audio_core
    vocroz::audio_core_fakes
    vocroz::common_fakes
    vocroz::control_protocol
    vocroz::control_protocol_fakes
    vocroz::network
    vocroz::session
    vocroz::session_fakes
    vocroz::transport
    vocroz::transport_fakes)
endif()
