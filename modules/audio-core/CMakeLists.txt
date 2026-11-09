# SPDX-License-Identifier: AGPL-3.0-only
#
# audio-core: 音声グラフ、ミキサー、ゲインランプ、安全リミッター、レベルメーターと、
# オーディオ入出力のポート。OS API に依存せず、Linux の CI で単体テストと RealtimeSanitizer の検査を行う。
add_library(vocroz_audio_core STATIC
  src/delay_line.cpp
  src/fader_bank.cpp
  src/feedback_analysis.cpp
  src/fixed_mixer_layout.cpp
  src/gain.cpp
  src/gain_ramp.cpp
  src/graph_description.cpp
  src/graph_exchange.cpp
  src/graph_validator.cpp
  src/latency_planner.cpp
  src/active_notes.cpp
  src/meter_bank.cpp
  src/midi_input_queue.cpp
  src/midi_message.cpp
  src/midi_router.cpp
  src/midi_timing.cpp
  src/mixer.cpp
  src/render_graph.cpp
  src/safety_limiter.cpp)
add_library(vocroz::audio_core ALIAS vocroz_audio_core)
target_include_directories(vocroz_audio_core PUBLIC include)
target_link_libraries(vocroz_audio_core PUBLIC vocroz::common)
vocroz_set_warnings(vocroz_audio_core)

if(VOCROZ_BUILD_TESTS)
  # AudioOutput、AudioInput、AudioSource、PluginProcessor のフェイク。device や transport のテストからも
  # #include "fakes/fake_audio_output.h" の形で使えるよう、ヘッダーだけのターゲットとして公開する。
  add_library(vocroz_audio_core_fakes INTERFACE)
  add_library(vocroz::audio_core_fakes ALIAS vocroz_audio_core_fakes)
  target_include_directories(vocroz_audio_core_fakes INTERFACE tests)
  target_link_libraries(vocroz_audio_core_fakes INTERFACE vocroz::audio_core)

  vocroz_add_unit_test(audio_core_tests
    tests/delay_line_test.cpp
    tests/fader_bank_test.cpp
    tests/feedback_analysis_test.cpp
    tests/fakes_test.cpp
    tests/fixed_mixer_layout_test.cpp
    tests/gain_ramp_test.cpp
    tests/gain_test.cpp
    tests/graph_exchange_test.cpp
    tests/graph_validator_test.cpp
    tests/latency_planner_test.cpp
    tests/active_notes_test.cpp
    tests/meter_bank_test.cpp
    tests/midi_input_queue_test.cpp
    tests/midi_message_test.cpp
    tests/midi_routing_test.cpp
    tests/midi_timing_test.cpp
    tests/mixer_test.cpp
    tests/realtime_sanitizer_test.cpp
    tests/render_graph_test.cpp
    tests/safety_limiter_test.cpp)
  # 内部クラス（src/ のヘッダー）も対象と同じモジュールのテストとして直接検証する。
  target_include_directories(audio_core_tests PRIVATE src)
  target_link_libraries(audio_core_tests PRIVATE vocroz::audio_core vocroz::audio_core_fakes
    vocroz::common_fakes)
endif()
