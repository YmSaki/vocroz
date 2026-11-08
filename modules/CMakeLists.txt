# SPDX-License-Identifier: AGPL-3.0-only
#
# 共有モジュール群。apps からのみ参照され、modules 同士の依存は
# AGENTS.md の「依存の向き」に従う。
add_subdirectory(common)
add_subdirectory(audio-core)
add_subdirectory(transport)
add_subdirectory(update)
