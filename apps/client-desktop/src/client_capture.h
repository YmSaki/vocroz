// SPDX-License-Identifier: AGPL-3.0-only
//
// session の CaptureControl の実装。入力デバイスの選択を、デバイスを開き直してキャプチャを
// 始めることで反映し、送信ミュートと送信レベルをオーディオスレッドの CaptureProcessor へ渡す。
// デバイスの抜去はデバイスのスレッドから届くので、アトミック変数に記録してコントロールスレッドで
// 取り出す（ClientRuntime が一覧を取り直して session に知らせる）。
#pragma once

#include <atomic>
#include <memory>
#include <optional>
#include <string>

#include "vocroz/audio_core/ports/audio_device_listener.h"
#include "vocroz/audio_core/ports/audio_input.h"
#include "vocroz/common/ports/logger.h"
#include "vocroz/session/ports/capture_control.h"

#include "capture_processor.h"
#include "input_device_opener.h"

namespace vocroz::client {

/// 子機のキャプチャの操作。
/// スレッド安全性: CaptureControl のメンバー関数と take_device_lost()
/// はコントロールスレッドから呼ぶ。 on_device_lost() はデバイスのスレッドから呼ばれる。
class ClientCapture final : public session::CaptureControl, public audio_core::AudioDeviceListener {
public:
    /// @param processor キャプチャしたブロックの処理。このオブジェクトより長く生存させる
    /// @param opener    入力デバイスを開く。このオブジェクトより長く生存させる
    /// @param logger    デバイスを開けなかったときの出力先。このオブジェクトより長く生存させる
    ClientCapture(CaptureProcessor& processor, InputDeviceOpener& opener, common::Logger& logger);
    /// キャプチャを止めてから破棄する。
    ~ClientCapture() override;
    ClientCapture(const ClientCapture&) = delete;
    ClientCapture& operator=(const ClientCapture&) = delete;

    /// デバイスを開き直してキャプチャを始める。開けなければ入力なしのままにする
    /// （別のデバイスへは切り替えない）。開けなかったデバイスがまだ一覧にあれば、出演者は
    /// 選び直せる。
    void select_input(const std::optional<std::string>& device_id) override;

    void set_send_muted(bool muted, std::uint32_t epoch) override;

    /// 送信レベルを反映する。範囲の検証は session が済ませている。
    void set_send_level(double level_db) override;

    /// 使用中の入力デバイスの抜去を記録する。
    void on_device_lost() noexcept override;

    /// 渡せなかった送信の設定を、オーディオスレッドへ渡し直す。コントロールスレッドの周期ごとに呼ぶ。
    /// オーディオスレッドが一時的に止まってキューが溜まりきると、最新の設定（ミュートを含む）が
    /// 落ちる。古い設定のまま再開しないよう、届くまで渡し直す。
    void flush_params();

    /// on_device_lost() の後に 1 回だけ true を返す。
    [[nodiscard]] bool take_device_lost() noexcept;

    /// キャプチャ中なら true を返す。
    [[nodiscard]] bool capturing() const noexcept {
        return input_ != nullptr;
    }

private:
    void push_params();

    CaptureProcessor& processor_;
    InputDeviceOpener& opener_;
    common::Logger& logger_;
    std::unique_ptr<audio_core::AudioInput> input_;
    SendParams params_;
    bool params_pending_ = false;
    std::atomic<bool> device_lost_{false};
};

}  // namespace vocroz::client
