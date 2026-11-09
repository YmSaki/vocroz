// SPDX-License-Identifier: AGPL-3.0-only
//
// UI とスナップショットで使う、エンジンが振る固定の entity_id。
// session がスナップショットとコマンドの照合に使い、pa-engine のミキサーの組み立てが
// ルーティングの送り元と送り先（"master" など）を引くのに使う。同じ値を 2 か所に書かないよう、
// ここで 1 か所に定める。UI のフェイクエンジンも同じ値を使う。
#pragma once

namespace vocroz::session {

/// セッション全体の entity_id。
inline constexpr const char* kSessionEntityId = "session";

/// Master ストリップの entity_id。ルーティングのセンドの送り元と送り先にもなる。
inline constexpr const char* kMasterEntityId = "master";

/// 子機モードの entity_id。
inline constexpr const char* kClientEntityId = "client";

/// ルーティング全体の entity_id。
inline constexpr const char* kRoutingEntityId = "routing";

/// SRTP 外部出力の entity_id。
inline constexpr const char* kExternalOutputEntityId = "external_output";

}  // namespace vocroz::session
