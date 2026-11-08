# SPDX-License-Identifier: AGPL-3.0-only
#
# transport: 子機と親機の間のメディアの伝送（ドメイン）。
# Opus のエンコードとデコード、ジッターバッファ、クロックドリフト補正、信号状態の判定を持つ。
# OS の API は呼ばない。ソケットは network のアダプターが実装する。
add_library(vocroz_transport STATIC)
target_sources(vocroz_transport PRIVATE
  src/drift_controller.cpp
  src/drift_resampler.cpp
  src/jitter_buffer.cpp
  src/media_frame.cpp
  src/opus_frame_decoder.cpp
  src/opus_frame_encoder.cpp
  src/playout_stream.cpp
  src/send_mute_tracker.cpp
  src/signal_monitor.cpp)
add_library(vocroz::transport ALIAS vocroz_transport)
target_include_directories(vocroz_transport
  PUBLIC include
  PRIVATE src)
target_link_libraries(vocroz_transport
  PUBLIC vocroz::common
  PRIVATE Opus::opus)
vocroz_set_warnings(vocroz_transport)

if(VOCROZ_BUILD_TESTS)
  vocroz_add_unit_test(transport_tests
    tests/drift_controller_test.cpp
    tests/drift_resampler_test.cpp
    tests/jitter_buffer_test.cpp
    tests/media_frame_test.cpp
    tests/opus_frame_decoder_test.cpp
    tests/opus_frame_encoder_test.cpp
    tests/playout_stream_test.cpp
    tests/send_mute_tracker_test.cpp
    tests/signal_monitor_test.cpp
    tests/signal_state_machine_test.cpp)
  # src/ のクラス（JitterBuffer など）は公開せず、同じモジュールのテストからだけ参照する。
  target_include_directories(transport_tests PRIVATE src)
  target_link_libraries(transport_tests PRIVATE vocroz::transport vocroz::common_fakes)
endif()
