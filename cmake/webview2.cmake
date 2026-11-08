# SPDX-License-Identifier: AGPL-3.0-only
#
# WebView2 SDK（NuGet のパッケージ Microsoft.Web.WebView2）を取得する。Windows だけ。
# JUCE の FindWebView2.cmake は、手元の NuGet のフォルダに入れたパッケージを探す前提で、自分では取得しない。
# ほかの外部ライブラリと同じく configure だけでビルドできるよう、URL と SHA-256 で固定して取得し、
# FindWebView2.cmake が探す場所（JUCE_WEBVIEW2_PACKAGE_LOCATION）に展開する
# （docs/adr/0020-ipc-pipe-and-webui-bridge.md）。ライセンスは BSD-3-Clause。
# 使用してよい範囲は modules/webui（JUCE の WebBrowserComponent 経由、AGENTS.md 4.2）。
if(NOT DEFINED JUCE_WEBVIEW2_PACKAGE_LOCATION)
  set(VOCROZ_WEBVIEW2_VERSION 1.0.3912.50)
  set(_vocroz_webview2_root "${CMAKE_BINARY_DIR}/_deps/webview2-packages")

  # .nupkg は zip だが、CMake は拡張子で展開の方法を決めるので、.zip の名前で保存する。
  FetchContent_Declare(webview2
    URL https://api.nuget.org/v3-flatcontainer/microsoft.web.webview2/${VOCROZ_WEBVIEW2_VERSION}/microsoft.web.webview2.${VOCROZ_WEBVIEW2_VERSION}.nupkg
    URL_HASH SHA256=8dd696301d8e5ad8389f26b636329bbf35754b793974e82168b1f6dda234eb07
    DOWNLOAD_NAME microsoft.web.webview2.${VOCROZ_WEBVIEW2_VERSION}.zip
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SOURCE_DIR "${_vocroz_webview2_root}/Microsoft.Web.WebView2.${VOCROZ_WEBVIEW2_VERSION}")
  # パッケージには CMakeLists.txt がないので、FetchContent_MakeAvailable は展開だけを行う。
  FetchContent_MakeAvailable(webview2)
  set(JUCE_WEBVIEW2_PACKAGE_LOCATION "${_vocroz_webview2_root}")
  unset(_vocroz_webview2_root)
endif()
