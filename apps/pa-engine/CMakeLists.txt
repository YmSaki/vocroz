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
  src/device_midi_inputs.cpp
  src/device_midi_relay.cpp
  src/engine_activity.cpp
  src/engine_mixer.cpp
  src/engine_output_devices.cpp
  src/engine_plugin_rack.cpp
  src/external_output_driver.cpp
  src/external_stream_worker.cpp
  src/host_key_loader.cpp
  src/host_peer_links.cpp
  src/host_rendezvous_driver.cpp
  src/invite_result_relay.cpp
  src/launch_options.cpp
  src/mixer_graph_builder.cpp
  src/network_external_socket_opener.cpp
  src/offline_rendezvous_client.cpp
  src/peer_receive_path.cpp
  src/peer_receive_relay.cpp
  src/playout_source.cpp
  src/plugin_scan_worker.cpp
  src/rendezvous_id_tap.cpp
  src/session_peer_events.cpp
  src/stderr_logger.cpp
  src/thread_peer_send_scheduler.cpp
  src/ui_event_publisher.cpp
  src/ui_message_receiver.cpp)
target_include_directories(pa_engine_core PUBLIC src ${CMAKE_CURRENT_BINARY_DIR}/generated)
target_link_libraries(pa_engine_core PUBLIC
  vocroz::network
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
    src/win/win32_named_mutex.cpp
    src/pa-engine.manifest)
  target_compile_definitions(pa_engine PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
  target_link_libraries(pa_engine PRIVATE
    vocroz::ipc
    vocroz::device
    vocroz::persistence
    vocroz::update
    dbghelp
    shell32
    ole32)
  if(VOCROZ_WITH_JUCE)
    # MIDI デバイスの入力と挿入のプラグインのため、JUCE のメッセージスレッドを 1 本持つ
    # （docs/adr/0039-pa-engine-juce-message-thread.md、docs/adr/0042-plugin-insert.md）。
    target_sources(pa_engine PRIVATE src/juce/juce_message_thread.cpp)
    target_compile_definitions(pa_engine PRIVATE VOCROZ_PA_ENGINE_HAS_JUCE)
    target_link_libraries(pa_engine PRIVATE vocroz::plugin_host vocroz::juce)
    # スキャン用ヘルパーは pa-engine.exe と同じフォルダから起動する。
    add_dependencies(pa_engine pa_plugin_scanner)
  endif()
endif()

if(VOCROZ_BUILD_TESTS)
  vocroz_add_unit_test(pa_engine_tests
    tests/control_loop_test.cpp
    tests/crash_dump_retention_test.cpp
    tests/device_loss_relay_test.cpp
    tests/device_midi_inputs_test.cpp
    tests/device_midi_relay_test.cpp
    tests/engine_activity_test.cpp
    tests/engine_mixer_test.cpp
    tests/engine_output_devices_test.cpp
    tests/engine_plugin_rack_test.cpp
    tests/external_output_driver_test.cpp
    tests/host_key_loader_test.cpp
    tests/host_peer_links_test.cpp
    tests/host_rendezvous_driver_test.cpp
    tests/invite_result_relay_test.cpp
    tests/launch_options_test.cpp
    tests/mixer_graph_builder_test.cpp
    tests/network_external_socket_opener_test.cpp
    tests/offline_rendezvous_client_test.cpp
    tests/peer_receive_relay_test.cpp
    tests/plugin_scan_worker_test.cpp
    tests/rendezvous_id_tap_test.cpp
    tests/thread_peer_send_scheduler_test.cpp
    tests/ui_event_publisher_test.cpp
    tests/ui_message_receiver_test.cpp)
  target_link_libraries(pa_engine_tests PRIVATE
    pa_engine_core
    vocroz::audio_core_fakes
    vocroz::common_fakes
    vocroz::control_protocol_fakes
    vocroz::session_fakes
    vocroz::transport_fakes)
  # network のフェイク（FakeUdpEndpoint）はターゲットにまとめられていないので、ディレクトリを参照する。
  target_include_directories(pa_engine_tests PRIVATE ${PROJECT_SOURCE_DIR}/modules/network/tests)

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
