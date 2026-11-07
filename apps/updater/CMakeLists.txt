# SPDX-License-Identifier: AGPL-3.0-only
#
# pa-updater: セッション終了後にだけ更新を適用する短命のプロセス（docs/adr/0009-installer-and-updater.md）。
# 更新の判断は vocroz::update にあり、ここは引数のパース、Windows のアダプター、組み立てだけを持つ。

# 配信先と公開鍵はビルド時に埋め込む。空のままのビルドは更新を確認せず、受け付けもしない。
set(VOCROZ_UPDATE_MANIFEST_URL "" CACHE STRING "更新マニフェストの HTTPS の URL")
set(VOCROZ_UPDATE_PUBLIC_KEY "" CACHE STRING "更新マニフェストの Ed25519 公開鍵（64 桁の小文字の 16 進数）")
configure_file(src/update_config.h.in ${CMAKE_CURRENT_BINARY_DIR}/generated/update_config.h @ONLY)

vocroz_add_app(pa_updater pa-updater src/main.cpp src/launch_options.cpp)
target_include_directories(pa_updater PRIVATE src ${CMAKE_CURRENT_BINARY_DIR}/generated)
target_link_libraries(pa_updater PRIVATE vocroz::update vocroz_version)

if(WIN32)
  target_sources(pa_updater PRIVATE
    src/composition_root.cpp
    src/win/file_install_layout.cpp
    src/win/file_settings_backup.cpp
    src/win/known_paths.cpp
    src/win/mutex_session_monitor.cpp
    src/win/process_health_checker.cpp
    src/win/tar_staged_package.cpp
    src/win/win32_process.cpp
    src/win/win32_text.cpp
    src/win/winhttp_update_source.cpp
    src/win/wintrust_code_signature_verifier.cpp
    src/pa-updater.manifest)
  target_compile_definitions(pa_updater PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
  target_link_libraries(pa_updater PRIVATE winhttp wintrust crypt32 shell32 ole32)
endif()

if(VOCROZ_BUILD_TESTS)
  vocroz_add_unit_test(pa_updater_tests tests/launch_options_test.cpp src/launch_options.cpp)
  target_include_directories(pa_updater_tests PRIVATE src)
  target_link_libraries(pa_updater_tests PRIVATE vocroz::common)
endif()
