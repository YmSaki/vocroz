// SPDX-License-Identifier: AGPL-3.0-only
//
// engine_plugin_rack.h の実装。

#include "engine_plugin_rack.h"

#include <cstdint>
#include <map>
#include <mutex>
#include <tuple>
#include <utility>

namespace vocroz::engine {

namespace session = vocroz::session;

/// メッセージスレッドの上の状態。post だけはグラフを手放したスレッドからも読むので mutex で守る。
struct EnginePluginRack::State {
    /// 読み込んだインスタンス 1 つ。
    struct Entry {
        std::unique_ptr<session::PluginInstance> instance;
        /// インスタンスが返した処理。グラフにはこれを指す別の共有ポインターを渡す。
        std::shared_ptr<audio_core::PluginProcessor> processor;
        /// session が手放した（release() した）。
        bool released = false;
        /// グラフに渡した処理の参照がまだ残っている。
        bool held_by_graph = false;
    };

    explicit State(Ports p) : ports(std::move(p)) {}

    bool post(Task task) {
        const std::lock_guard lock{post_mutex};
        return accepting && ports.post(std::move(task));
    }

    void stop_posting() {
        const std::lock_guard lock{post_mutex};
        accepting = false;
    }

    // インスタンスはエディターを閉じてから破棄する。エディターはインスタンスを参照するため。
    void destroy(std::map<std::uint64_t, Entry>::iterator it) {
        if (editors) {
            editors->close(*it->second.instance);
        }
        entries.erase(it);
    }

    void graph_released(std::uint64_t serial) {
        const auto it = entries.find(serial);
        if (it == entries.end()) {
            return;
        }
        it->second.held_by_graph = false;
        if (it->second.released) {
            destroy(it);
        }
    }

    Ports ports;
    std::mutex post_mutex;
    bool accepting = true;
    // ここから下はメッセージスレッドからだけ触る。
    std::unique_ptr<session::PluginLoader> loader;
    std::unique_ptr<session::PluginEditorHost> editors;
    std::map<std::uint64_t, Entry> entries;
    /// 挿入の entity_id から、今のインスタンスの通し番号を引く。同じ entity_id で挿し直した
    /// とき、前のインスタンスと取り違えないよう、インスタンスは通し番号で区別する。
    std::map<std::string, std::uint64_t, std::less<>> current;
    std::uint64_t next_serial = 1;
};

EnginePluginRack::EnginePluginRack(Ports ports)
    : state_(std::make_shared<State>(std::move(ports))) {}

EnginePluginRack::~EnginePluginRack() {
    // destroy_all() を呼ばずに破棄されても、手放された処理がこのオブジェクトの後で
    // メッセージスレッドへ積もうとしないようにする。
    state_->stop_posting();
}

void EnginePluginRack::load(const std::string& insert_id, session::PluginLoadTicket ticket,
                            const session::PluginDescription& description) {
    auto state = state_;
    const bool posted = state->post([state, insert_id, ticket, description] {
        if (!state->loader) {
            state->loader = state->ports.make_loader();
            state->editors = state->ports.make_editor_host();
        }
        auto loaded = state->loader->load(description);
        if (!loaded) {
            state->ports.logger.log(common::LogLevel::kWarning,
                                    "挿入のプラグインを読み込めませんでした");
            state->ports.deliver(insert_id, ticket, common::make_unexpected(loaded.error()));
            return;
        }
        const auto serial = state->next_serial++;
        State::Entry entry;
        entry.instance = std::move(loaded).value();
        entry.processor = entry.instance->processor();
        entry.held_by_graph = true;
        auto* raw = entry.processor.get();
        state->entries.emplace(serial, std::move(entry));
        state->current[insert_id] = serial;
        // グラフが最後の参照を手放したら、メッセージスレッドで破棄してよいかを確かめる。
        // 状態は弱参照で持ち、ラックの破棄後に手放されても何もしない。
        const std::weak_ptr<State> weak = state;
        std::shared_ptr<audio_core::PluginProcessor> handed{
            raw, [weak, serial](audio_core::PluginProcessor* /*unused*/) {
                if (const auto owner = weak.lock()) {
                    std::ignore = owner->post([weak, serial] {
                        if (const auto s = weak.lock()) {
                            s->graph_released(serial);
                        }
                    });
                }
            }};
        state->ports.deliver(insert_id, ticket, std::move(handed));
    });
    if (!posted) {
        // メッセージスレッドが止まっている。結果を返さないので、挿入は読み込み中のまま無音になる。
        state->ports.logger.log(common::LogLevel::kWarning,
                                "メッセージスレッドが動いていないため、プラグインを読み込めません");
    }
}

void EnginePluginRack::release(const std::string& insert_id) {
    auto state = state_;
    std::ignore = state->post([state, insert_id] {
        const auto current = state->current.find(insert_id);
        if (current == state->current.end()) {
            return;
        }
        const auto it = state->entries.find(current->second);
        state->current.erase(current);
        if (it == state->entries.end()) {
            return;
        }
        it->second.released = true;
        if (state->editors) {
            // 外した挿入のエディターは、グラフが処理を手放す前でもすぐに閉じる。
            state->editors->close(*it->second.instance);
        }
        if (!it->second.held_by_graph) {
            state->destroy(it);
        }
    });
}

void EnginePluginRack::open_editor(const std::string& insert_id, const std::string& title) {
    auto state = state_;
    const bool posted = state->post([state, insert_id, title] {
        const auto current = state->current.find(insert_id);
        if (current == state->current.end() || !state->editors) {
            // 開く前に外された。外した挿入のエディターは開かない。
            return;
        }
        const auto it = state->entries.find(current->second);
        if (it == state->entries.end()) {
            return;
        }
        if (!state->editors->open(*it->second.instance, title)) {
            state->ports.logger.log(common::LogLevel::kWarning,
                                    "プラグインのエディターを開けませんでした");
            state->ports.editor_failed(insert_id);
        }
    });
    if (!posted) {
        state->ports.logger.log(
            common::LogLevel::kWarning,
            "メッセージスレッドが動いていないため、プラグインのエディターを開けません");
    }
}

void EnginePluginRack::destroy_all() {
    state_->stop_posting();
    if (state_->editors) {
        state_->editors->close_all();
    }
    state_->current.clear();
    state_->entries.clear();
    state_->editors.reset();
    state_->loader.reset();
}

}  // namespace vocroz::engine
