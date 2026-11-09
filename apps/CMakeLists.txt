# SPDX-License-Identifier: AGPL-3.0-only
#
# 実行ファイル群。各アプリは対応するフェーズまで最小限のエントリポイントだけを持つ。
#   remote-pa  : 親機の操作卓 UI のホスト。--client で出演者用クライアントとして動く
#   pa-engine  : 親機の音声エンジン（セッション・音声の状態をすべて保持する）
#   pa-updater : セッションがないときだけ動く短命の更新プロセス
#   vocroz     : 現行版の remote-pa を起動する起動スタブ（Windows だけ）
#   pa-plugin-scanner : pa-engine が起動する VST3 のスキャン用ヘルパー（JUCE があるときだけ）

# 実行ファイルを作り、--version のスモークテストを登録する。
#   vocroz_add_app(<target> <output_name> <source>...)
function(vocroz_add_app target output_name)
  add_executable(${target} ${ARGN})
  set_target_properties(${target} PROPERTIES OUTPUT_NAME ${output_name})
  target_link_libraries(${target} PRIVATE vocroz::common)
  vocroz_set_warnings(${target})
  # 配布物では、バージョン別フォルダ（app-x.y.z、docs/adr/0013-installer-and-updater.md）に入る。
  install(TARGETS ${target} RUNTIME DESTINATION app)
  add_test(NAME ${target}.version COMMAND ${target} --version)
  set_tests_properties(${target}.version PROPERTIES
    PASS_REGULAR_EXPRESSION "^VOCROZ ${output_name} ${PROJECT_VERSION}")
endfunction()

add_subdirectory(client-desktop)
add_subdirectory(pa-desktop)
add_subdirectory(pa-engine)
add_subdirectory(updater)
add_subdirectory(launcher)
add_subdirectory(plugin-scanner)

# VC++ ランタイムを、バージョン別フォルダと起動スタブの隣に同梱する（app-local 配置）。
# 管理者権限の要る再頒布パッケージを使わず、ユーザー単位の導入のままにするため。
if(MSVC)
  set(CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS_SKIP TRUE)
  include(InstallRequiredSystemLibraries)
  install(PROGRAMS ${CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS} DESTINATION app)
  install(PROGRAMS ${CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS} DESTINATION .)
endif()
