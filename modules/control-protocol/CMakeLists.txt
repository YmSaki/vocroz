# SPDX-License-Identifier: AGPL-3.0-only
#
# control-protocol: UI とエンジン間のコマンドとイベントの DTO、JSON のエンコードとデコード（信頼境界）、
# メッセージを運ぶ MessageChannel ポートと、2 つの経路をつなぐ ChannelRelay。
# nlohmann/json は実装の中だけで使い、公開ヘッダーには出さない。
add_library(vocroz_control_protocol STATIC
  src/channel_relay.cpp
  src/json_codec.cpp
  src/wire/command_codec.cpp
  src/wire/entity_codec.cpp
  src/wire/event_codec.cpp
  src/wire/object_reader.cpp)
add_library(vocroz::control_protocol ALIAS vocroz_control_protocol)
target_include_directories(vocroz_control_protocol
  PUBLIC include
  PRIVATE src)
target_link_libraries(vocroz_control_protocol
  PUBLIC vocroz::common
  PRIVATE nlohmann_json::nlohmann_json)
vocroz_set_warnings(vocroz_control_protocol)

if(VOCROZ_BUILD_TESTS)
  # MessageChannel のフェイク。ipc と webui のテストからも #include "fakes/fake_message_channel.h" で使える。
  add_library(vocroz_control_protocol_fakes INTERFACE)
  add_library(vocroz::control_protocol_fakes ALIAS vocroz_control_protocol_fakes)
  target_include_directories(vocroz_control_protocol_fakes INTERFACE tests)
  target_link_libraries(vocroz_control_protocol_fakes INTERFACE vocroz::control_protocol)

  vocroz_add_unit_test(control_protocol_tests
    tests/channel_relay_test.cpp
    tests/json_codec_test.cpp
    tests/json_codec_examples_test.cpp)
  target_link_libraries(control_protocol_tests PRIVATE
    vocroz::control_protocol
    vocroz::control_protocol_fakes
    nlohmann_json::nlohmann_json)
  # 全メッセージの JSON の例（UI のテストと共有する）の場所。
  target_compile_definitions(control_protocol_tests PRIVATE
    VOCROZ_CONTROL_PROTOCOL_EXAMPLES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/data")
endif()
