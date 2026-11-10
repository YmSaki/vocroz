// SPDX-License-Identifier: AGPL-3.0-only
//
// 挿入のプラグインを JUCE のメッセージスレッドで読み込み、破棄する PluginRack
// （docs/adr/0042-plugin-insert.md、docs/adr/0039-pa-engine-juce-message-thread.md）。
//
// session はコントロールスレッドから依頼するだけで、読み込みと破棄はメッセージスレッドで行う。
// JUCE のプラグインはメッセージスレッドで作り、メッセージスレッドで破棄する必要があるため。
// 読み込みの結果はメッセージスレッドから deliver で返し、コンポジションルートがコントロール
// スレッドへ移して HostSession::on_plugin_loaded() に渡す。
//
// グラフに渡す処理は、インスタンスが持つ処理を指す別の共有ポインターにする。グラフが最後の
// 参照を手放したら（どのスレッドでも）メッセージスレッドへ知らせ、release() 済みのインスタンスは
// そこで初めて破棄する。オーディオスレッドが使っている処理を消さず、JUCE のプラグインを
// コントロールスレッドで破棄しないため。
//
// JUCE の型は使わないので、Linux の CI でフェイクのローダーと即時実行の post で単体テストできる。
#pragma once

#include <functional>
#include <memory>
#include <string>

#include "vocroz/audio_core/ports/plugin_processor.h"
#include "vocroz/common/ports/logger.h"
#include "vocroz/common/result.h"
#include "vocroz/session/ports/plugin_editor_host.h"
#include "vocroz/session/ports/plugin_loader.h"
#include "vocroz/session/ports/plugin_rack.h"

namespace vocroz::engine {

/// メッセージスレッドで読み込む PluginRack。
/// スレッド安全性: load() と release() はコントロールスレッドから呼ぶ。destroy_all() は
/// メッセージスレッドから呼ぶ。グラフに渡した処理は、どのスレッドで手放してもよい
/// （オーディオスレッドを除く。古いグラフはオーディオスレッドの外で破棄する）。
class EnginePluginRack final : public session::PluginRack {
public:
    /// メッセージスレッドで実行する処理。
    using Task = std::function<void()>;
    /// 読み込みの結果。
    using LoadResult =
        common::Result<std::shared_ptr<audio_core::PluginProcessor>, session::PluginLoadError>;

    /// 注入する依存。どれもこのオブジェクトより長く生存させる（AGENTS.md 4.3）。
    struct Ports {
        /// 処理をメッセージスレッドへ積む（JuceMessageThread::post()）。積めなければ false。
        std::function<bool(Task)> post;
        /// ローダーを作る。最初の読み込みのとき、メッセージスレッドで 1 回だけ呼ぶ。
        std::function<std::unique_ptr<session::PluginLoader>()> make_loader;
        /// エディターのホストを作る。最初の読み込みのとき、メッセージスレッドで 1 回だけ呼ぶ。
        std::function<std::unique_ptr<session::PluginEditorHost>()> make_editor_host;
        /// 読み込みの結果を返す。メッセージスレッドから呼ぶので、受け取る側がコントロール
        /// スレッドへ移す。
        std::function<void(const std::string& insert_id, session::PluginLoadTicket ticket,
                           LoadResult result)>
            deliver;
        common::Logger& logger;
    };

    explicit EnginePluginRack(Ports ports);
    ~EnginePluginRack() override;
    EnginePluginRack(const EnginePluginRack&) = delete;
    EnginePluginRack& operator=(const EnginePluginRack&) = delete;

    void load(const std::string& insert_id, session::PluginLoadTicket ticket,
              const session::PluginDescription& description) override;
    void release(const std::string& insert_id) override;

    /// エディターをすべて閉じ、すべてのインスタンスとローダーを破棄する。以降に手放された
    /// 処理はメッセージスレッドへ知らせない。JUCE を終える前に、メッセージスレッドの最後の
    /// 処理（JuceMessageThread::stop() の last）から呼ぶ。オーディオ出力は先に閉じておくこと
    /// （グラフが処理を持っていても、もう呼ばれない状態にする）。
    void destroy_all();

private:
    struct State;
    std::shared_ptr<State> state_;
};

}  // namespace vocroz::engine
