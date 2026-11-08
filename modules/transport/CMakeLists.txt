# SPDX-License-Identifier: AGPL-3.0-only
#
# transport: 子機と親機の間の UDP パケットの形式、Noise_IK による鍵交換、AEAD、リプレイ排除と、
# その上の信頼制御層（再送、キープアライブ、死活監視、再接続のバックオフ）。
# ソケットの実装は持たず（network が担う）、バイト列の検証と暗号化だけを行う。
# libsodium と noise-c はこのモジュールの中だけで使い、公開ヘッダーに型を出さない（AGENTS.md 4.2）。
add_library(vocroz_transport STATIC)
target_sources(vocroz_transport PRIVATE
  src/crypto_random_source.cpp
  src/detail/crypto_utils.cpp
  src/liveness_monitor.cpp
  src/noise_ik_handshake.cpp
  src/reconnect_backoff.cpp
  src/reliable_control_channel.cpp
  src/reliable_receive_lane.cpp
  src/reliable_send_lane.cpp
  src/rtt_estimator.cpp
  src/secure_channel.cpp
  src/secure_control_packet_sink.cpp
  src/state/handshake_state_machine.cpp
  src/static_key_pair.cpp
  src/wire/control_frame_codec.cpp
  src/wire/media_payload_codec.cpp
  src/wire/packet_header_codec.cpp)
add_library(vocroz::transport ALIAS vocroz_transport)
target_include_directories(vocroz_transport
  PUBLIC include
  PRIVATE src)
target_link_libraries(vocroz_transport
  PUBLIC vocroz::common
  PRIVATE noise::protocol sodium)
vocroz_set_warnings(vocroz_transport)

if(VOCROZ_BUILD_TESTS)
  vocroz_add_unit_test(transport_tests
    tests/liveness_monitor_test.cpp
    tests/media_payload_test.cpp
    tests/noise_ik_handshake_test.cpp
    tests/packet_header_test.cpp
    tests/reconnect_backoff_test.cpp
    tests/reliable_control_channel_test.cpp
    tests/secure_channel_test.cpp
    tests/secure_control_packet_sink_test.cpp
    tests/static_key_pair_test.cpp)
  target_include_directories(transport_tests PRIVATE tests)
  target_link_libraries(transport_tests PRIVATE vocroz::transport vocroz::common_fakes)
endif()
