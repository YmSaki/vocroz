# SPDX-License-Identifier: AGPL-3.0-only
#
# 結合テスト。モジュールの部品を製品と同じ順序で組み立て、シミュレーションのネットワーク越しに動かす。
# すべて仮想時刻で動くので、実時間を待たず、OS もデバイスも要らない（Linux の CI で回す）。
vocroz_add_unit_test(integration_tests
  client_inputs_test.cpp
  control_faults_test.cpp
  ensemble_test.cpp
  latency_test.cpp
  media_faults_test.cpp)
target_link_libraries(integration_tests PRIVATE vocroz::test_harness)
