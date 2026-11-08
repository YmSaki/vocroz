# SPDX-License-Identifier: AGPL-3.0-only
#
# transport: 子機と親機の間の UDP パケットの形式、Noise_IK による鍵交換、AEAD、リプレイ排除。
# ソケットの実装は持たず（network が担う）、バイト列の検証と暗号化だけを行う。
# libsodium と noise-c はこのモジュールの中だけで使い、公開ヘッダーに型を出さない（AGENTS.md 4.2）。
add_library(vocroz_transport STATIC)
target_sources(vocroz_transport PRIVATE
  src/detail/crypto_utils.cpp
  src/noise_ik_handshake.cpp
  src/secure_channel.cpp
  src/state/handshake_state_machine.cpp
  src/static_key_pair.cpp
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
    tests/media_payload_test.cpp
    tests/noise_ik_handshake_test.cpp
    tests/packet_header_test.cpp
    tests/secure_channel_test.cpp
    tests/static_key_pair_test.cpp)
  target_link_libraries(transport_tests PRIVATE vocroz::transport)
endif()
