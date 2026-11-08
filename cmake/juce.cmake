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
