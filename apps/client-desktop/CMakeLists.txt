# SPDX-License-Identifier: AGPL-3.0-only
#
# 出演者用クライアント。単独の実行ファイルではなく remote-pa に組み込み、--client で起動する。
add_library(vocroz_client STATIC src/client_app.cpp)
add_library(vocroz::client ALIAS vocroz_client)
target_include_directories(vocroz_client PUBLIC include)
target_link_libraries(vocroz_client PUBLIC vocroz::common)
vocroz_set_warnings(vocroz_client)
