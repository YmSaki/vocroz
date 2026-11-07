# SPDX-License-Identifier: AGPL-3.0-only
#
# vocroz.exe: インストール先に固定で置く起動スタブ。現行版の remote-pa を選んで起動し、すぐに終了する
# （docs/adr/0009-installer-and-updater.md）。受け取った引数は解釈せず、そのまま渡すので、
# 他のアプリと違って --version のスモークテストは登録しない（--version も remote-pa に渡すため）。
if(WIN32)
  add_executable(vocroz_launcher WIN32
    src/main.cpp
    src/composition_root.cpp
    src/command_line.cpp
    src/win/win32_launch.cpp)
  set_target_properties(vocroz_launcher PROPERTIES OUTPUT_NAME vocroz)
  target_include_directories(vocroz_launcher PRIVATE src)
  target_compile_definitions(vocroz_launcher PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
  target_link_libraries(vocroz_launcher PRIVATE vocroz::update vocroz_version user32)
  vocroz_set_warnings(vocroz_launcher)
  install(TARGETS vocroz_launcher RUNTIME DESTINATION .)
endif()

# コマンドラインの切り出しは OS に依存しないので、Linux の CI でもテストする。
if(VOCROZ_BUILD_TESTS)
  vocroz_add_unit_test(launcher_tests tests/command_line_test.cpp src/command_line.cpp)
  target_include_directories(launcher_tests PRIVATE src)
endif()
