// SPDX-License-Identifier: AGPL-3.0-only
//
// 親機モードで pa-engine につなぐ手順（docs/adr/0030-remote-pa-assembly.md）。
// 1. パイプにつなぐ。つながれば、動いているエンジンをそのまま使う（再アタッチ）
// 2. つながらなければ、エンジンを 1 回だけ起動し、つながるまで間隔を空けて試し直す。
//    エンジンが終了コード 3（すでに動いている）で終わったら、そのエンジンへの接続を続ける
// 3. 時間内につながらないか、エンジンがほかの理由で終わったら、失敗として知らせる
// 1 回の手順で起動するのは 1 回だけにする。エンジンが落ちるたびに自動で起動し続けると、
// 起動直後に落ちる不具合で起動と異常終了を繰り返すため（docs/adr/0008-runtime-behavior.md）。
// もう一度試すかどうかは、失敗を知らされた利用者が決める。
#pragma once

#include <memory>

#include "vocroz/common/ports/clock.h"
#include "vocroz/control_protocol/ports/message_channel.h"

#include "engine_connector.h"
#include "engine_launcher.h"

namespace vocroz::pa_desktop {

/// つなげなかった理由。
enum class AttachFailure {
    kRefused,  ///< パイプが使えない（ほかのユーザーのもの、なりすまし）
    kLaunchFailed,  ///< エンジンを起動できなかった（実行ファイルがないなど）
    kEngineExited,  ///< 起動したエンジンが、つながる前に終わった
    kTimedOut,      ///< 時間内につながらなかった
};

/// 手順の結果を受け取るリスナー。
/// スレッド安全性: EngineAttacher::tick() の中から、コントロールスレッドで呼ばれる。
class EngineAttachListener {
public:
    virtual ~EngineAttachListener() = default;
    /// つながった。
    /// @param channel エンジンとの経路。所有権を渡す
    virtual void on_engine_attached(std::unique_ptr<control_protocol::MessageChannel> channel) = 0;
    /// つなげなかった。もう一度試すなら begin() を呼ぶ。
    virtual void on_engine_attach_failed(AttachFailure failure) = 0;
};

/// エンジンへの接続の手順。
/// スレッド安全性: すべてコントロールスレッド（メッセージスレッド）から呼ぶ。
class EngineAttacher {
public:
    /// @param connector パイプへの接続。このオブジェクトより長く生存させる
    /// @param launcher  エンジンの起動。このオブジェクトより長く生存させる
    /// @param clock     時刻。このオブジェクトより長く生存させる
    /// @param listener  結果の渡し先。このオブジェクトより長く生存させる
    EngineAttacher(EngineConnector& connector, EngineLauncher& launcher, const common::Clock& clock,
                   EngineAttachListener& listener) noexcept;

    /// 手順を始める（すでに途中なら最初からやり直す）。最初の接続は次の tick() で試す。
    void begin() noexcept;

    /// 手順を進める。周期的に呼ぶ。手順の途中でなければ何もしない。
    void tick();

    /// 手順の途中なら true。
    [[nodiscard]] bool attaching() const noexcept {
        return attaching_;
    }

private:
    void fail(AttachFailure failure);

    EngineConnector& connector_;
    EngineLauncher& launcher_;
    const common::Clock& clock_;
    EngineAttachListener& listener_;

    bool attaching_ = false;
    bool launched_ = false;
    common::Clock::TimePoint next_attempt_{};
    common::Clock::TimePoint deadline_{};
};

}  // namespace vocroz::pa_desktop
