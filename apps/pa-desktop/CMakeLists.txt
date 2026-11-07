# SPDX-License-Identifier: AGPL-3.0-only
#
# remote-pa: 親機 UI のホスト。子機モードのため vocroz::client をリンクする。
vocroz_add_app(remote_pa remote-pa src/main.cpp)
target_link_libraries(remote_pa PRIVATE vocroz::client)
