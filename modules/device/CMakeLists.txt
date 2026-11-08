# SPDX-License-Identifier: AGPL-3.0-only
#
# device: WASAPI によるオーディオ入出力（audio-core の AudioOutput、AudioInput、AudioDeviceCatalog の実装）。
# エンドポイント ID による永続的なデバイスの指定、低遅延共有モード（IAudioClient3）、抜去の検知を持つ。
# 抜去は通知するだけで、ミュートの判断はしない（session と Mixer::engage_safety_mute()）。
# Windows 以外では、OS に依存しない判断（ストリームの開き方）だけをビルドしてテストする。
add_library(vocroz_device STATIC
  src/detail/stream_plan.cpp)
add_library(vocroz::device ALIAS vocroz_device)
target_include_directories(vocroz_device
  PUBLIC include
  PRIVATE src)
target_link_libraries(vocroz_device PUBLIC vocroz::audio_core vocroz::common)
if(WIN32)
  target_sources(vocroz_device PRIVATE
    src/win/com_utils.cpp
    src/win/factories.cpp
    src/win/mmcss_registration.cpp
    src/win/wasapi_audio_device_catalog.cpp
    src/win/wasapi_audio_input.cpp
    src/win/wasapi_audio_output.cpp
    src/win/wasapi_capture_pump.cpp
    src/win/wasapi_endpoint.cpp
    src/win/wasapi_render_pump.cpp
    src/win/wasapi_stream.cpp
    src/win/wide_string.cpp)
  target_compile_definitions(vocroz_device PRIVATE NOMINMAX UNICODE _UNICODE)
  # ole32: COM、avrt: MMCSS（AvSetMmThreadCharacteristics）
  target_link_libraries(vocroz_device PRIVATE ole32 avrt)
endif()
vocroz_set_warnings(vocroz_device)

if(VOCROZ_BUILD_TESTS)
  vocroz_add_unit_test(device_tests
    tests/stream_plan_test.cpp)
  if(WIN32)
    # 実際の WASAPI を呼ぶテスト。CI のランナーにはオーディオデバイスがないことがあるので、
    # デバイスが要るテストはデバイスがなければスキップする。
    target_sources(device_tests PRIVATE tests/wasapi_device_test.cpp)
    target_compile_definitions(device_tests PRIVATE NOMINMAX UNICODE _UNICODE)
  endif()
  # 内部の判断（src/detail/）も対象と同じモジュールのテストとして直接検証する。
  target_include_directories(device_tests PRIVATE src)
  target_link_libraries(device_tests PRIVATE vocroz::device vocroz::common_fakes)
endif()
