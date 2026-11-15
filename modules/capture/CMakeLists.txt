# SPDX-License-Identifier: AGPL-3.0-only
#
# capture: WASAPI のループバックによるアプリ音声の収集（audio-core の AudioInput の実装）。
# ほかのアプリが再生する音（このプロセスの音を除く）を、48kHz Float32 のステレオで渡す
# （docs/adr/0035-client-input-expansion.md）。
# WASAPI は Windows にしかなく、OS に依存しない判断もないので、Windows でだけビルドする。
if(NOT WIN32)
  return()
endif()

add_library(vocroz_capture STATIC
  src/win/com_utils.cpp
  src/win/default_render_watcher.cpp
  src/win/factories.cpp
  src/win/mmcss_registration.cpp
  src/win/process_loopback_activation.cpp
  src/win/wasapi_loopback_input.cpp)
add_library(vocroz::capture ALIAS vocroz_capture)
target_include_directories(vocroz_capture
  PUBLIC include
  PRIVATE src)
target_link_libraries(vocroz_capture PUBLIC vocroz::audio_core vocroz::common)
target_compile_definitions(vocroz_capture PRIVATE NOMINMAX UNICODE _UNICODE)
# ole32: COM、avrt: MMCSS（AvSetMmThreadCharacteristics）、
# mmdevapi: プロセスループバックの活性化（ActivateAudioInterfaceAsync）
target_link_libraries(vocroz_capture PRIVATE ole32 avrt mmdevapi)
vocroz_set_warnings(vocroz_capture)

if(VOCROZ_BUILD_TESTS)
  # 実際の WASAPI を呼ぶテスト。CI のランナーには再生デバイスがないことがあるので、
  # デバイスが要るテストはデバイスがなければスキップする。
  vocroz_add_unit_test(capture_tests
    tests/support/tone_player.cpp
    tests/wasapi_loopback_input_test.cpp)
  target_compile_definitions(capture_tests PRIVATE NOMINMAX UNICODE _UNICODE)
  target_include_directories(capture_tests PRIVATE tests)
  # ole32: テストのプロセスから音を鳴らす再生ストリーム（tests/support/tone_player）
  target_link_libraries(capture_tests PRIVATE vocroz::capture vocroz::common_fakes ole32)
endif()
