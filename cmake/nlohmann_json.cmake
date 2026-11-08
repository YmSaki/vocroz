# SPDX-License-Identifier: AGPL-3.0-only
#
# nlohmann/json を取得し、ターゲット nlohmann_json::nlohmann_json を定義する（docs/adr/0010-external-libraries.md）。
# リポジトリ全体ではなく、リリースに含まれるヘッダーと CMake だけの json.tar.xz を使う（約 100KB）。
if(NOT TARGET nlohmann_json::nlohmann_json)
  set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
  set(JSON_Install OFF CACHE BOOL "" FORCE)

  FetchContent_Declare(nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
    URL_HASH SHA256=42f6e95cad6ec532fd372391373363b62a14af6d771056dbfc86160e6dfff7aa
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SYSTEM)  # 外部ヘッダーの警告を出さない
  FetchContent_MakeAvailable(nlohmann_json)
endif()
