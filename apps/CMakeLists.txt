# SPDX-License-Identifier: AGPL-3.0-only
#
# 実行ファイル群。各アプリは対応するフェーズまで最小限のエントリポイントだけを持つ。
#   remote-pa  : 親機の操作卓 UI のホスト。--client で出演者用クライアントとして動く
#   pa-engine  : 親機の音声エンジン（セッション・音声の状態をすべて保持する）
#   pa-updater : セッションがないときだけ動く短命の更新プロセス

# 実行ファイルを作り、--version のスモークテストを登録する。
#   vocroz_add_app(<target> <output_name> <source>...)
function(vocroz_add_app target output_name)
  add_executable(${target} ${ARGN})
  set_target_properties(${target} PROPERTIES OUTPUT_NAME ${output_name})
  target_link_libraries(${target} PRIVATE vocroz::common)
  vocroz_set_warnings(${target})
  add_test(NAME ${target}.version COMMAND ${target} --version)
  set_tests_properties(${target}.version PROPERTIES
    PASS_REGULAR_EXPRESSION "^VOCROZ ${output_name} ${PROJECT_VERSION}")
endfunction()

add_subdirectory(client-desktop)
add_subdirectory(pa-desktop)
add_subdirectory(pa-engine)
add_subdirectory(updater)
