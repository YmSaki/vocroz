# SPDX-License-Identifier: AGPL-3.0-only
#
# update: 更新マニフェストの検証、適用の判断、更新トランザクションとロールバックの判定、
# 現行版の選択（docs/adr/0009-installer-and-updater.md）。OS の API は呼ばない。
add_library(vocroz_update STATIC
  src/semantic_version.cpp
  src/manifest_verifier.cpp
  src/update_policy.cpp
  src/update_applier.cpp
  src/update_checker.cpp
  src/package_verification.cpp
  src/launch_selection.cpp
  src/session_lock.cpp
  src/state/update_state_machine.cpp
  src/wire/manifest_codec.cpp
  src/wire/install_pointer_codec.cpp
  src/detail/text_utils.cpp
  src/detail/sodium_init.cpp)
add_library(vocroz::update ALIAS vocroz_update)
target_include_directories(vocroz_update
  PUBLIC include
  PRIVATE src)
target_link_libraries(vocroz_update
  PUBLIC vocroz::common
  PRIVATE sodium)
vocroz_set_warnings(vocroz_update)

if(VOCROZ_BUILD_TESTS)
  vocroz_add_unit_test(update_tests
    tests/semantic_version_test.cpp
    tests/manifest_verifier_test.cpp
    tests/update_policy_test.cpp
    tests/update_state_machine_test.cpp
    tests/update_applier_test.cpp
    tests/update_checker_test.cpp
    tests/install_pointer_test.cpp
    tests/launch_selection_test.cpp
    tests/session_lock_test.cpp)
  # 遷移表は src/ の非公開ヘッダーなので、テストからだけ src/ を参照する。
  # テストで署名を作るために libsodium を直接使う。
  target_include_directories(update_tests PRIVATE src tests)
  target_link_libraries(update_tests PRIVATE vocroz::update sodium)
endif()
