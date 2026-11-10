// SPDX-License-Identifier: AGPL-3.0-only
//
// プラグインを読み込まない PluginRack。
// JUCE を使わない pa-engine のビルドで HostSession
// に渡す（docs/adr/0042-plugin-insert.md）。プラグインの一覧を渡していないので、検証で挿入はすべて
// UNKNOWN_PLUGIN になり、ここが呼ばれることはない。万一呼ばれても結果を返さないので、挿入は
// 読み込み中のまま、その経路は無音になる（フェイルセーフ）。
#pragma once

#include <string>

#include "vocroz/session/ports/plugin_rack.h"

namespace vocroz::engine {

/// 何も読み込まない PluginRack。
/// スレッド安全性: コントロールスレッドから呼ぶ。状態を持たない。
class NoPluginRack final : public session::PluginRack {
public:
    void load(const std::string& /*insert_id*/, session::PluginLoadTicket /*ticket*/,
              const session::PluginDescription& /*description*/) override {}

    void release(const std::string& /*insert_id*/) override {}

    void open_editor(const std::string& /*insert_id*/, const std::string& /*title*/) override {}
};

}  // namespace vocroz::engine
