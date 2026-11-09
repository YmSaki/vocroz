# SPDX-License-Identifier: AGPL-3.0-only
#
# JUCE を取得し、juce_add_* 関数と juce::juce_* のターゲットを使えるようにする。
# JUCE は AGPLv3 と商用ライセンスのデュアルライセンスで、VOCROZ は AGPLv3 で利用する。
# 使用してよい範囲はアダプターレイヤーと apps/（AGENTS.md 4.2）。
# VOCROZ_WITH_JUCE が OFF のときはこのファイルを読み込まない（ルートの CMakeLists.txt）。
if(NOT COMMAND juce_add_console_app)
  # JUCE のリポジトリは履歴が大きいので、タグで浅く取得し、コミットのハッシュを別に検査する。
  set(VOCROZ_JUCE_TAG 9.0.5)
  set(VOCROZ_JUCE_COMMIT d60b7a78030661574a615b586d9325ba7a7fa7dd)

  FetchContent_Declare(juce
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG ${VOCROZ_JUCE_TAG}
    GIT_SHALLOW TRUE
    SYSTEM)  # 外部ヘッダーの警告を出さない
  FetchContent_MakeAvailable(juce)
  vocroz_verify_git_revision(JUCE "${juce_SOURCE_DIR}" ${VOCROZ_JUCE_COMMIT})
endif()

# JUCE のモジュールは、リンクしたターゲットごとにソースがコンパイルされる。複数のアダプターが
# 別々にリンクすると同じシンボルが重複するので、使うモジュールを静的ライブラリ vocroz::juce に
# 1 回だけコンパイルし、JUCE を使うアダプター（webui、plugin-host、device）はすべてこれをリンクする
# （docs/adr/0020-ipc-pipe-and-webui-bridge.md、docs/adr/0034-single-juce-target.md）。
# モジュールを足すときは、下の target_link_libraries の PRIVATE に加える。
if(NOT TARGET vocroz_juce)
  add_library(vocroz_juce STATIC)
  add_library(vocroz::juce ALIAS vocroz_juce)
  target_link_libraries(vocroz_juce
    PRIVATE
      # webui: WebBrowserComponent
      juce::juce_gui_extra
      # plugin-host: VST3 のスキャン、読み込み、処理、エディターのウィンドウ
      juce::juce_audio_processors
      # device: MIDI デバイスの列挙、入力、抜去の検知（MidiInput）
      juce::juce_audio_devices
    PUBLIC juce::juce_recommended_config_flags)
  target_compile_definitions(vocroz_juce PUBLIC
    # ネットワークは network モジュールが Winsock で扱うので、JUCE の curl は使わない。
    JUCE_USE_CURL=0
    JUCE_WEB_BROWSER=1
    # VST3 のホスト。VST2、AU、LV2 などは扱わない（docs/adr/0009-beta-module-layout.md）。
    JUCE_PLUGINHOST_VST3=1
    JUCE_STANDALONE_APPLICATION=1
    # WebView2 の読み込み用のライブラリを静的にリンクし、WebView2Loader.dll を配布せずに済ませる。
    JUCE_USE_WIN_WEBVIEW2_WITH_STATIC_LINKING=1)
  # PRIVATE でリンクしたモジュールのヘッダーと定義を、利用側にも見せる。
  # ソースは INTERFACE_SOURCES なので利用側には伝わらず、コンパイルはここだけで行われる。
  target_include_directories(vocroz_juce SYSTEM INTERFACE
    $<TARGET_PROPERTY:vocroz_juce,INCLUDE_DIRECTORIES>)
  target_compile_definitions(vocroz_juce INTERFACE
    $<TARGET_PROPERTY:vocroz_juce,COMPILE_DEFINITIONS>)

  if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # 手元の Linux で JUCE を使うとき（CI は使わない）。WebKitGTK と GTK のヘッダーを使う。
    target_link_libraries(vocroz_juce PRIVATE juce::pkgconfig_JUCE_BROWSER_LINUX_DEPS)
  endif()
  if(WIN32)
    include(${CMAKE_CURRENT_LIST_DIR}/webview2.cmake)
    list(APPEND CMAKE_MODULE_PATH "${JUCE_CMAKE_UTILS_DIR}")
    find_package(WebView2 REQUIRED)
    target_link_libraries(vocroz_juce PRIVATE juce::juce_webview2)
    # WebView2.h は利用側のコンパイルには不要だが、JUCE の定義で WebView2 を使うことを伝えるため、
    # インクルードディレクトリも公開しておく。
    target_include_directories(vocroz_juce SYSTEM INTERFACE ${WebView2_INCLUDE_DIRS})
  endif()
endif()
