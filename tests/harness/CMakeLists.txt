# SPDX-License-Identifier: AGPL-3.0-only
#
# test_harness: 結合テストのために transport の部品を製品と同じ順序で組み立て、
# シミュレーションのネットワーク（netsim）につなぐ（テスト専用）。
# 子機の送信側、親機の受信側、仮想時刻での駆動、遅延計測と、session をつないだ親機と子機
# （SimulatedHost、SimulatedClient、EnsembleSimulation）を持つ。
# 製品の組み立て（client-desktop、pa-engine、session）が入ったら、結合テストはそちらを直接使い、
# ここには製品にない部分（フェイクデバイスと仮想時刻の駆動、計測）だけを残す
# （docs/adr/0025-integration-test-harness.md）。
add_library(vocroz_test_harness STATIC
  src/ensemble_simulation.cpp
  src/harness_mixer.cpp
  src/latency_measurement.cpp
  src/media_link_simulation.cpp
  src/media_receiver.cpp
  src/media_sender.cpp
  src/secure_link.cpp
  src/simulated_client.cpp
  src/simulated_datagram_socket.cpp
  src/simulated_host.cpp)
add_library(vocroz::test_harness ALIAS vocroz_test_harness)
target_include_directories(vocroz_test_harness PUBLIC include)
target_link_libraries(vocroz_test_harness
  PUBLIC
    vocroz::netsim
    vocroz::transport
    vocroz::session
    vocroz::session_fakes
    vocroz::control_protocol
    vocroz::audio_core
    vocroz::common_fakes)
vocroz_set_warnings(vocroz_test_harness)
