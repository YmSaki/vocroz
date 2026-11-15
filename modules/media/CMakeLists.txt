# SPDX-License-Identifier: AGPL-3.0-only
#
# media: JUCE による録音のファイルの書き出し（WAV、FLAC）と、再生するファイルの読み込みとデコード
# （audio-core の AudioFileWriter、AudioFileReader の実装。docs/adr/0043-recording-and-media-player.md）。
# 空き容量の判断（src/detail/）は JUCE を使わないので、VOCROZ_WITH_JUCE が OFF でもビルドして
# Linux の CI でテストする。
add_library(vocroz_media STATIC
  src/detail/free_space_guard.cpp)
add_library(vocroz::media ALIAS vocroz_media)
target_include_directories(vocroz_media PUBLIC include PRIVATE src)
target_link_libraries(vocroz_media PUBLIC vocroz::audio_core vocroz::common)
if(VOCROZ_WITH_JUCE)
  target_sources(vocroz_media PRIVATE
    src/juce/juce_audio_file_reader.cpp
    src/juce/juce_audio_file_writer.cpp
    src/juce/juce_factories.cpp)
  # JUCE のモジュールは vocroz::juce に 1 回だけコンパイルしたものを使う
  # （docs/adr/0034-single-juce-target.md）。テストは src/juce/ の内部クラスを JUCE の型で
  # 検証するので、ヘッダーと定義も利用側に見せる（PUBLIC）。
  target_link_libraries(vocroz_media PUBLIC vocroz::juce)
endif()
vocroz_set_warnings(vocroz_media)

if(VOCROZ_BUILD_TESTS)
  vocroz_add_unit_test(media_tests
    tests/free_space_guard_test.cpp)
  if(VOCROZ_WITH_JUCE)
    # 実際のファイルを一時フォルダに書いて読み戻す。
    target_sources(media_tests PRIVATE tests/juce_audio_file_test.cpp)
  endif()
  # 内部の判断（src/detail/）と JUCE の具象クラスも、対象と同じモジュールのテストとして直接検証する。
  target_include_directories(media_tests PRIVATE src)
  target_link_libraries(media_tests PRIVATE vocroz::media)
endif()
