# SPDX-License-Identifier: AGPL-3.0-only
#
# pa-engine: 親機の音声エンジン（docs/adr/0028-engine-assembly.md）。
# OS に依存しない組み立ての部品（コントロールループ、ミキサーとルーティングのグラフ、UI との
# やり取り、自動終了の規則）は静的ライブラリ pa_engine_core にまとめ、Linux でも単体テストする。
# コンポジションルートと Windows のアダプター（src/win/）は Windows のビルドだけに入れる。

set(VOCROZ_RENDEZVOUS_URL "" CACHE STRING "招待 URL の基点にするランデブーサーバーの HTTPS の URL")
configure_file(src/engine_config.h.in ${CMAKE_CURRENT_BINARY_DIR}/generated/engine_config.h @ONLY)

add_library(pa_engine_core STATIC
  src/control_loop.cpp
  src/crash_dump_retention.cpp
  src/crypto_secure_random.cpp
  src/device_loss_relay.cpp
  src/engine_activity.cpp
  src/engine_mixer.cpp
  src/launch_options.cpp
  src/memory_settings_store.cpp
  src/mixer_graph_builder.cpp
  src/offline_rendezvous_client.cpp
  src/stderr_logger.cpp
  src/ui_event_publisher.cpp
  src/ui_message_receiver.cpp)
target_include_directories(pa_engine_core PUBLIC src ${CMAKE_CURRENT_BINARY_DIR}/generated)
target_link_libraries(pa_engine_core PUBLIC
  vocroz::session
  vocroz::transport
  vocroz::audio_core
  vocroz::control_protocol
  vocroz::common)
vocroz_set_warnings(pa_engine_core)

vocroz_add_app(pa_engine pa-engine src/main.cpp)
target_link_libraries(pa_engine PRIVATE pa_engine_core)
if(WIN32)
  target_sources(pa_engine PRIVATE
    src/composition_root.cpp
    src/win/console_stop_signal.cpp
    src/win/execution_state_sleep_inhibitor.cpp
    src/win/known_paths.cpp
    src/win/minidump_crash_handler.cpp
    src/win/win32_named_mutex.cpp)
  target_compile_definitions(pa_engine PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
  target_link_libraries(pa_engine PRIVATE
    vocroz::ipc
    vocroz::device
    vocroz::update
    dbghelp
    shell32
    ole32)
endif()

if(VOCROZ_BUILD_TESTS)
  vocroz_add_unit_test(pa_engine_tests
    tests/control_loop_test.cpp
    tests/crash_dump_retention_test.cpp
    tests/device_loss_relay_test.cpp
    tests/engine_activity_test.cpp
    tests/engine_mixer_test.cpp
    tests/launch_options_test.cpp
    tests/memory_settings_store_test.cpp
    tests/mixer_graph_builder_test.cpp
    tests/offline_rendezvous_client_test.cpp
    tests/ui_event_publisher_test.cpp
    tests/ui_message_receiver_test.cpp)
  target_link_libraries(pa_engine_tests PRIVATE
    pa_engine_core
    vocroz::audio_core_fakes
    vocroz::common_fakes
    vocroz::control_protocol_fakes)

  # 起動のスモークテスト。pa-engine.exe を別プロセスで起動し、名前付きパイプでつなぐ。
  if(WIN32)
    vocroz_add_unit_test(pa_engine_smoke_tests tests/engine_smoke_test.cpp)
    target_compile_definitions(pa_engine_smoke_tests PRIVATE
      WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
    target_link_libraries(pa_engine_smoke_tests PRIVATE
      vocroz::ipc
      vocroz::control_protocol
      vocroz::common_fakes)
    # テストはテストの実行ファイルと同じディレクトリの pa-engine.exe を起動する。
    add_dependencies(pa_engine_smoke_tests pa_engine)
  endif()
endif()
