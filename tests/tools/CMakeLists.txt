# SPDX-License-Identifier: AGPL-3.0-only
#
# 試験ツール。製品には含めず、インストーラーにも入れない。

# 端から端までの遅延の計測（仮想時刻で動くので OS に依存しない）。
add_executable(vocroz-latency latency/main.cpp)
target_link_libraries(vocroz-latency PRIVATE vocroz::test_harness)
vocroz_set_warnings(vocroz-latency)

# 実ソケットの UDP 障害注入プロキシ。POSIX のソケットで書いているので Linux だけでビルドする。
# Windows の実機確認では WSL か、Linux の機械を経路の途中に置いて使う（tests/README.md）。
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  add_library(vocroz_udp_fault_proxy STATIC udp-fault-proxy/udp_fault_proxy.cpp)
  target_include_directories(vocroz_udp_fault_proxy PUBLIC udp-fault-proxy)
  target_link_libraries(vocroz_udp_fault_proxy PUBLIC vocroz::netsim)
  vocroz_set_warnings(vocroz_udp_fault_proxy)

  add_executable(vocroz-udp-fault-proxy udp-fault-proxy/main.cpp)
  target_link_libraries(vocroz-udp-fault-proxy PRIVATE vocroz_udp_fault_proxy)
  vocroz_set_warnings(vocroz-udp-fault-proxy)

  vocroz_add_unit_test(udp_fault_proxy_tests udp-fault-proxy/udp_fault_proxy_test.cpp)
  target_link_libraries(udp_fault_proxy_tests PRIVATE vocroz_udp_fault_proxy)
endif()

# A/V 同期の試験パターンの発生とずれの計測（Issue #144）。
# 標準ライブラリだけで書いているので、Windows と Linux の両方でビルドする。
add_library(vocroz_av_sync STATIC
  av-sync/av_sync_pattern.cpp
  av-sync/av_sync_detector.cpp
  av-sync/wav_file.cpp
  av-sync/y4m_file.cpp
  av-sync/av_sync_measurement.cpp)
target_include_directories(vocroz_av_sync PUBLIC av-sync)
target_link_libraries(vocroz_av_sync PUBLIC vocroz::common)
vocroz_set_warnings(vocroz_av_sync)

add_executable(vocroz-av-sync av-sync/main.cpp)
target_link_libraries(vocroz-av-sync PRIVATE vocroz_av_sync)
vocroz_set_warnings(vocroz-av-sync)

vocroz_add_unit_test(av_sync_tests
  av-sync/av_sync_pattern_test.cpp
  av-sync/av_sync_detector_test.cpp
  av-sync/wav_file_test.cpp
  av-sync/y4m_file_test.cpp
  av-sync/av_sync_measurement_test.cpp)
target_link_libraries(av_sync_tests PRIVATE vocroz_av_sync)
