# SPDX-License-Identifier: AGPL-3.0-only
#
# 外部ライブラリのリンク確認テスト。cmake/<ライブラリ名>.cmake で取得したライブラリが、
# このリポジトリのコンパイラ設定でビルドとリンクができ、最小限の呼び出しが動くことを確かめる。
# ライブラリを使うモジュールができる前に、取得やビルドの問題を CI で見つけるために置く。
# ライブラリの使い方のテストは、使うモジュールの tests/ に書く。
vocroz_add_unit_test(opus_link_test opus_link_test.cpp)
target_link_libraries(opus_link_test PRIVATE Opus::opus)

vocroz_add_unit_test(libsodium_link_test libsodium_link_test.cpp)
target_link_libraries(libsodium_link_test PRIVATE sodium)

vocroz_add_unit_test(noise_c_link_test noise_c_link_test.cpp)
target_link_libraries(noise_c_link_test PRIVATE noise::protocol)

vocroz_add_unit_test(libsrtp_link_test libsrtp_link_test.cpp)
target_link_libraries(libsrtp_link_test PRIVATE libSRTP::srtp2)

vocroz_add_unit_test(nlohmann_json_link_test nlohmann_json_link_test.cpp)
target_link_libraries(nlohmann_json_link_test PRIVATE nlohmann_json::nlohmann_json)

if(WIN32)
  vocroz_add_unit_test(miniupnpc_link_test miniupnpc_link_test.cpp)
  target_link_libraries(miniupnpc_link_test PRIVATE miniupnpc::miniupnpc)
endif()

if(VOCROZ_WITH_JUCE)
  vocroz_add_unit_test(juce_link_test juce_link_test.cpp)
  target_link_libraries(juce_link_test PRIVATE juce::juce_core)
  # ネットワークと WebView を使わない最小構成にする。curl や WebView2 の SDK がなくてもビルドできるようにするため。
  target_compile_definitions(juce_link_test PRIVATE
    JUCE_USE_CURL=0
    JUCE_WEB_BROWSER=0
    JUCE_STANDALONE_APPLICATION=1)
endif()
