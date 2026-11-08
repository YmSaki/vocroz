# SPDX-License-Identifier: AGPL-3.0-only
#
# transport: 子機と親機の間のメディアと制御の伝送（ドメイン）。
# UDP パケットの形式、Noise_IK による鍵交換、AEAD、リプレイ排除と、
# Opus のエンコードとデコード、ジッターバッファ、クロックドリフト補正、信号状態の判定、
# 信頼制御層（再送、キープアライブ、死活監視、再接続のバックオフ）を持つ。
# ソケットの実装は持たず（network が担う）、OS の API は呼ばない。
# libsodium、noise-c、libopus はこのモジュールの中だけで使い、公開ヘッダーに型を出さない（AGENTS.md 4.2）。
add_library(vocroz_transport STATIC)
target_sources(vocroz_transport PRIVATE
  src/crypto_random_source.cpp
  src/detail/crypto_utils.cpp
  src/drift_controller.cpp
  src/drift_resampler.cpp
  src/jitter_buffer.cpp
  src/liveness_monitor.cpp
  src/media_frame.cpp
  src/noise_ik_handshake.cpp
  src/opus_frame_decoder.cpp
  src/opus_frame_encoder.cpp
  src/playout_stream.cpp
  src/reconnect_backoff.cpp
  src/reliable_control_channel.cpp
  src/reliable_receive_lane.cpp
  src/reliable_send_lane.cpp
  src/rtt_estimator.cpp
  src/secure_channel.cpp
  src/secure_control_packet_sink.cpp
  src/send_mute_tracker.cpp
  src/signal_monitor.cpp
  src/state/handshake_state_machine.cpp
  src/static_key_pair.cpp
  src/wire/control_frame_codec.cpp
  src/wire/media_payload_codec.cpp
  src/wire/packet_header_codec.cpp)
add_library(vocroz::transport ALIAS vocroz_transport)
target_include_directories(vocroz_transport
  PUBLIC include
  PRIVATE src)
# audio-core は VOCROZ_NONBLOCKING（vocroz/audio_core/realtime.h）のためだけに使う。
target_link_libraries(vocroz_transport
  PUBLIC vocroz::common vocroz::audio_core
  PRIVATE noise::protocol sodium Opus::opus)
vocroz_set_warnings(vocroz_transport)

if(VOCROZ_BUILD_TESTS)
  vocroz_add_unit_test(transport_tests
    tests/drift_controller_test.cpp
    tests/drift_resampler_test.cpp
    tests/jitter_buffer_test.cpp
    tests/liveness_monitor_test.cpp
    tests/media_frame_test.cpp
    tests/media_payload_test.cpp
    tests/noise_ik_handshake_test.cpp
    tests/opus_frame_decoder_test.cpp
    tests/opus_frame_encoder_test.cpp
    tests/packet_header_test.cpp
    tests/playout_stream_test.cpp
    tests/reconnect_backoff_test.cpp
    tests/reliable_control_channel_test.cpp
    tests/secure_channel_test.cpp
    tests/secure_control_packet_sink_test.cpp
    tests/send_mute_tracker_test.cpp
    tests/signal_monitor_test.cpp
    tests/signal_state_machine_test.cpp
    tests/static_key_pair_test.cpp)
  # src/ のクラス（JitterBuffer など）は公開せず、同じモジュールのテストからだけ参照する。
  # tests/ はこのモジュールのフェイク（tests/fakes/）を参照するため。
  target_include_directories(transport_tests PRIVATE src tests)
  target_link_libraries(transport_tests PRIVATE vocroz::transport vocroz::common_fakes)
endif()
