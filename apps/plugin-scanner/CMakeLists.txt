# SPDX-License-Identifier: AGPL-3.0-only
#
# pa-plugin-scanner: VST3 のスキャン用ヘルパー（docs/adr/0009-beta-module-layout.md）。
# pa-engine がプラグインファイル 1 つにつき 1 回起動する。スキャンの処理は vocroz::plugin_host にあり、
# ここは引数のパースと結果の出力だけを持つ。JUCE を使うので VOCROZ_WITH_JUCE が ON のときだけ作る。
if(VOCROZ_WITH_JUCE)
  vocroz_add_app(pa_plugin_scanner pa-plugin-scanner src/main.cpp src/launch_options.cpp)
  target_include_directories(pa_plugin_scanner PRIVATE src)
  target_link_libraries(pa_plugin_scanner PRIVATE vocroz::plugin_host)
  if(WIN32)
    target_sources(pa_plugin_scanner PRIVATE
      src/win/crash_dialogs.cpp
      src/pa-plugin-scanner.manifest)
    target_compile_definitions(pa_plugin_scanner PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
  endif()
endif()

# 引数のパースは JUCE を使わないので、Linux の CI でもテストする。
if(VOCROZ_BUILD_TESTS)
  vocroz_add_unit_test(pa_plugin_scanner_tests tests/launch_options_test.cpp src/launch_options.cpp)
  target_include_directories(pa_plugin_scanner_tests PRIVATE src)
  target_link_libraries(pa_plugin_scanner_tests PRIVATE vocroz::common)
endif()
