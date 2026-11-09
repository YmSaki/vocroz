# SPDX-License-Identifier: AGPL-3.0-only
#
# miniupnpc（UPnP-IGD のクライアント）を取得し、ターゲット miniupnpc::miniupnpc を定義する。
# 使用してよい範囲は network だけで、UPnP-IGD による自動ポート開放にだけ使う
# （AGENTS.md 4.2、docs/adr/0021-udp-socket-and-nat-traversal.md）。
# Windows のアダプター（modules/network/src/win/）だけが使うので、Windows のビルドでだけ取得する。
# リリースの tar ボールがなく、リポジトリには miniupnpd なども含まれるので、
# タグで浅く取得してコミットのハッシュを検査し、miniupnpc のディレクトリだけをビルドする。
if(NOT TARGET miniupnpc::miniupnpc)
  set(UPNPC_BUILD_STATIC ON CACHE BOOL "" FORCE)
  set(UPNPC_BUILD_SHARED OFF CACHE BOOL "" FORCE)
  set(UPNPC_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(UPNPC_BUILD_SAMPLE OFF CACHE BOOL "" FORCE)
  set(UPNPC_NO_INSTALL ON CACHE BOOL "" FORCE)

  FetchContent_Declare(miniupnpc
    GIT_REPOSITORY https://github.com/miniupnp/miniupnp.git
    GIT_TAG miniupnpc_2_3_3
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR miniupnpc
    SYSTEM)  # 外部ヘッダーの警告を出さない
  FetchContent_MakeAvailable(miniupnpc)
  vocroz_verify_git_revision(miniupnpc "${miniupnpc_SOURCE_DIR}"
    bf4215a7574f88aa55859db9db00e3ae58cf42d6)
endif()
