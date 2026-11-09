# SPDX-License-Identifier: AGPL-3.0-only
#
# plugin-host: JUCE による VST3 のスキャン、読み込み、処理（docs/adr/0009-beta-module-layout.md）。
# スキャン結果のスキーマ（pa-plugin-scanner との間のメッセージ）は JUCE を使わないので、
# VOCROZ_WITH_JUCE が OFF でもビルドして Linux の CI でテストする。
add_library(vocroz_plugin_host STATIC
  src/scan_messages.cpp)
add_library(vocroz::plugin_host ALIAS vocroz_plugin_host)
target_include_directories(vocroz_plugin_host PUBLIC include PRIVATE src)
target_link_libraries(vocroz_plugin_host
  PUBLIC vocroz::audio_core vocroz::common
  PRIVATE nlohmann_json::nlohmann_json)
vocroz_set_warnings(vocroz_plugin_host)

if(VOCROZ_WITH_JUCE)
  target_sources(vocroz_plugin_host PRIVATE
    src/juce/juce_helper_scan.cpp
    src/juce/juce_plugin_description.cpp
    src/juce/juce_plugin_processor.cpp
    src/juce/juce_vst3_loader.cpp
    src/juce/juce_vst3_scanner.cpp)
  # JUCE のモジュールのソースはこのライブラリの中で 1 回だけコンパイルする（PRIVATE）。
  # 利用側（pa-engine、pa-plugin-scanner、テスト）でもう一度コンパイルすると、シンボルが重複するため。
  # GUI を使わない headless のモジュールにし、エディターのウィンドウを加えるときに切り替える。
  target_link_libraries(vocroz_plugin_host PRIVATE juce::juce_audio_processors_headless)
  target_compile_definitions(vocroz_plugin_host PRIVATE
    JUCE_PLUGINHOST_VST3=1
    JUCE_USE_CURL=0
    JUCE_WEB_BROWSER=0
    JUCE_STANDALONE_APPLICATION=1)
  # テストは src/juce/ の内部クラスを JUCE の型で検証するので、JUCE のヘッダーを読むための
  # インクルードパスと定義だけを利用側に渡す（ソースは渡さない）。
  target_include_directories(vocroz_plugin_host SYSTEM INTERFACE
    $<TARGET_PROPERTY:vocroz_plugin_host,INCLUDE_DIRECTORIES>)
  target_compile_definitions(vocroz_plugin_host INTERFACE
    $<TARGET_PROPERTY:vocroz_plugin_host,COMPILE_DEFINITIONS>)
endif()

if(VOCROZ_BUILD_TESTS)
  vocroz_add_unit_test(plugin_host_tests tests/scan_messages_test.cpp)
  target_link_libraries(plugin_host_tests PRIVATE vocroz::plugin_host)

  if(VOCROZ_WITH_JUCE)
    # スキャン用ヘルパーの代わりに、落ちる、止まる、壊れた出力を書く、などの振る舞いをする実行ファイル。
    add_executable(fake_plugin_scanner tests/fake_plugin_scanner.cpp)
    target_link_libraries(fake_plugin_scanner PRIVATE vocroz::plugin_host)

    target_sources(plugin_host_tests PRIVATE
      tests/juce_helper_scan_test.cpp
      tests/juce_plugin_processor_test.cpp)
    target_include_directories(plugin_host_tests PRIVATE src)
    target_link_libraries(plugin_host_tests PRIVATE vocroz::audio_core_fakes)
    add_dependencies(plugin_host_tests fake_plugin_scanner)
    target_compile_definitions(plugin_host_tests PRIVATE
      "VOCROZ_FAKE_PLUGIN_SCANNER=\"$<TARGET_FILE:fake_plugin_scanner>\"")
  endif()
endif()
