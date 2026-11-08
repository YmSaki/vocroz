# SPDX-License-Identifier: AGPL-3.0-only
#
# libopus を取得し、ターゲット Opus::opus を定義する。使用してよい範囲は transport だけ（AGENTS.md 4.2）。
# リリースの tar ボールを使うのは、Git のソースには 1.5 以降の DNN のモデルデータが含まれず、
# 取得時にスクリプトでダウンロードする手順が必要になるため。
if(NOT TARGET Opus::opus)
  # 浮動小数点版を使う。オーディオグラフが Float32 で処理するため、変換を挟まずに済む。
  set(OPUS_FIXED_POINT OFF CACHE BOOL "" FORCE)
  set(OPUS_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
  set(OPUS_BUILD_TESTING OFF CACHE BOOL "" FORCE)
  set(OPUS_BUILD_SHARED_LIBRARY OFF CACHE BOOL "" FORCE)
  set(OPUS_INSTALL_PKG_CONFIG_MODULE OFF CACHE BOOL "" FORCE)
  set(OPUS_INSTALL_CMAKE_CONFIG_MODULE OFF CACHE BOOL "" FORCE)
  # DRED と OSCE は Opus 1.5 の深層学習による拡張で、追加の遅延と CPU 負荷がかかる。
  # 低遅延モード（OPUS_APPLICATION_RESTRICTED_LOWDELAY）では使わないので無効にする（docs/adr/0002-opus-low-delay.md）。
  set(OPUS_DRED OFF CACHE BOOL "" FORCE)
  set(OPUS_OSCE OFF CACHE BOOL "" FORCE)

  FetchContent_Declare(opus
    URL https://github.com/xiph/opus/releases/download/v1.5.2/opus-1.5.2.tar.gz
    URL_HASH SHA256=65c1d2f78b9f2fb20082c38cbe47c951ad5839345876e46941612ee87f9a7ce1
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SYSTEM)  # 外部ヘッダーの警告を出さない
  FetchContent_MakeAvailable(opus)
endif()
