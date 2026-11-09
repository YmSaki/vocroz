# SPDX-License-Identifier: AGPL-3.0-only
#
# persistence: 設定のアトミックな保存とバックアップ、ログファイルへの出力とローテーション、
# マスキング済みの診断レポート、DPAPI による秘密情報の保護（アダプター、docs/adr/0027-persistence.md）。
# session の SettingsStore と SecretStore、common の Logger を実装する。
# nlohmann/json は実装の中だけで使い、公開ヘッダーには出さない。
add_library(vocroz_persistence STATIC
  src/atomic_file.cpp
  src/detail/time_format.cpp
  src/diagnostic_report.cpp
  src/factories.cpp
  src/json_settings_store.cpp
  src/rotating_file_logger.cpp
  src/secret_file_store.cpp
  src/secret_masker.cpp
  src/settings_codec.cpp)
if(WIN32)
  target_sources(vocroz_persistence PRIVATE
    src/win/dpapi_data_protector.cpp
    src/win/dpapi_factories.cpp
    src/win/win32_durable_io.cpp)
  target_compile_definitions(vocroz_persistence PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
  target_link_libraries(vocroz_persistence PRIVATE crypt32)
else()
  # 製品は Windows だけだが、保存の規則を Linux の CI で単体テストするために POSIX の実装を使う。
  target_sources(vocroz_persistence PRIVATE src/posix/posix_durable_io.cpp)
endif()
add_library(vocroz::persistence ALIAS vocroz_persistence)
target_include_directories(vocroz_persistence
  PUBLIC include
  PRIVATE src)
target_link_libraries(vocroz_persistence
  PUBLIC vocroz::common vocroz::session
  PRIVATE nlohmann_json::nlohmann_json)
vocroz_set_warnings(vocroz_persistence)

if(VOCROZ_BUILD_TESTS)
  set(persistence_test_sources
    tests/atomic_file_test.cpp
    tests/diagnostic_report_test.cpp
    tests/json_settings_store_test.cpp
    tests/rotating_file_logger_test.cpp
    tests/secret_file_store_test.cpp
    tests/secret_masker_test.cpp
    tests/settings_codec_test.cpp
    tests/time_format_test.cpp)
  if(WIN32)
    list(APPEND persistence_test_sources tests/dpapi_data_protector_test.cpp)
  endif()
  vocroz_add_unit_test(persistence_tests ${persistence_test_sources})
  # 単体テストは src/ の内部の型（AtomicFile、変換、マスキング）も直接確かめる。
  target_include_directories(persistence_tests PRIVATE src tests)
  target_link_libraries(persistence_tests PRIVATE
    vocroz::persistence
    vocroz::session_fakes
    vocroz::common_fakes)
endif()
