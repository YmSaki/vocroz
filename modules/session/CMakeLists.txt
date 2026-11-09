# SPDX-License-Identifier: AGPL-3.0-only
#
# session: 親機と子機のセッション管理（ドメイン）。状態機械、招待のパースと照合、承認と再接続、
# 参加者とチャンネルストリップの状態、コマンドの適用と認可、子機と親機の間の制御メッセージの形式。
# OS の API を呼ばず、外部 I/O（ミキサー、接続、ランデブーサーバー、設定、UI）はポート経由で使う。
add_library(vocroz_session STATIC
  src/client_session.cpp
  src/detail/text_utils.cpp
  src/host_command_handler.cpp
  src/host_context.cpp
  src/host_peer_handler.cpp
  src/host_session.cpp
  src/invite_book.cpp
  src/invite_code.cpp
  src/participant_registry.cpp
  src/request_deduplicator.cpp
  src/routing_book.cpp
  src/routing_delay.cpp
  src/routing_validator.cpp
  src/state/client_state_machine.cpp
  src/state/connection_state_machine.cpp
  src/state/output_device_state_machine.cpp
  src/state/session_state_machine.cpp
  src/wire/peer_message_codec.cpp)
add_library(vocroz::session ALIAS vocroz_session)
target_include_directories(vocroz_session
  PUBLIC include
  PRIVATE src)
target_link_libraries(vocroz_session
  PUBLIC vocroz::common vocroz::control_protocol
  PRIVATE vocroz::audio_core)
vocroz_set_warnings(vocroz_session)

if(VOCROZ_BUILD_TESTS)
  # ポートのフェイク。persistence や network、apps のテストからも
  # #include "fakes/fake_settings_store.h" などの形で使える。
  add_library(vocroz_session_fakes INTERFACE)
  add_library(vocroz::session_fakes ALIAS vocroz_session_fakes)
  target_include_directories(vocroz_session_fakes INTERFACE tests)
  target_link_libraries(vocroz_session_fakes INTERFACE vocroz::session)

  vocroz_add_unit_test(session_tests
    tests/client_session_test.cpp
    tests/client_state_machine_test.cpp
    tests/connection_state_machine_test.cpp
    tests/host_session_test.cpp
    tests/invite_book_test.cpp
    tests/invite_code_test.cpp
    tests/output_device_state_machine_test.cpp
    tests/participant_registry_test.cpp
    tests/peer_message_codec_test.cpp
    tests/request_deduplicator_test.cpp
    tests/routing_book_test.cpp
    tests/routing_validator_test.cpp
    tests/session_state_machine_test.cpp
    tests/text_utils_test.cpp)
  # 単体テストは src/ の内部の型（状態機械、制御メッセージの形式）も直接確かめる。
  target_include_directories(session_tests PRIVATE src)
  target_link_libraries(session_tests PRIVATE
    vocroz::session
    vocroz::session_fakes
    vocroz::common_fakes)
endif()
