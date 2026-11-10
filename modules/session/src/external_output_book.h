// SPDX-License-Identifier: AGPL-3.0-only
//
// SRTP 外部出力の状態（設定、鍵、送信の状態、直前の結果）と、その遷移の規則
// （docs/adr/0040-srtp-external-output.md）。
// - 設定と鍵は、止めている間だけ変えられる（送信中の宛先や鍵を黙って変えない）
// - 送信と接続テストは、宛先と鍵がそろっているときだけ始められる（鍵がなければ平文を送る部品も
//   組み立てられないが、ここでも先に断って理由を UI に返す）
// - 起動時は必ず止めた状態から始める（初期ミュートの製品要件）
// 送信の開始と停止（ExternalOutputControl）、UI への通知、保存は呼び出し側が行い、
// ここは状態と鍵だけを持つ。
#pragma once

#include <optional>

#include "vocroz/common/result.h"
#include "vocroz/control_protocol/entities.h"
#include "vocroz/control_protocol/states.h"
#include "vocroz/session/ports/external_output_control.h"
#include "vocroz/transport/srtp_master_key.h"

namespace vocroz::session {

/// SRTP 外部出力の状態。
/// revision は設定、鍵、送信の開始と停止、接続テストの開始で進める。接続テストの終了は
/// エンジンが観測した結果なので進めない。
/// スレッド安全性: コントロールスレッドからだけ使う。
class ExternalOutputBook {
public:
    ExternalOutputBook();

    /// UI に送る外部出力の状態。
    [[nodiscard]] const control_protocol::ExternalOutput& info() const noexcept {
        return info_;
    }

    /// 保存した設定と鍵を戻す。送信の状態は止めたまま（起動時に 1 回だけ呼ぶ）。revision は
    /// 進めない。
    /// @param config 保存した設定
    /// @param key    保存した鍵。なければ std::nullopt
    void restore(const control_protocol::ExternalOutputConfig& config,
                 std::optional<transport::SrtpMasterKey> key);

    /// 設定を変える。
    /// @return 止めていなければ INVALID_STATE
    [[nodiscard]] common::Result<void, control_protocol::ErrorCode> set_config(
        const control_protocol::ExternalOutputConfig& config);

    /// 鍵を変える。
    /// @return 止めていなければ INVALID_STATE
    [[nodiscard]] common::Result<void, control_protocol::ErrorCode> set_key(
        const transport::SrtpMasterKey& key);

    /// 今の設定で送るときの送り方と鍵を返す。
    /// @return 宛先か鍵が未設定なら INVALID_STATE
    [[nodiscard]] common::Result<ExternalOutputPlan, control_protocol::ErrorCode> plan() const;

    /// plan() が成功したときの鍵。
    [[nodiscard]] const std::optional<transport::SrtpMasterKey>& key() const noexcept {
        return key_;
    }

    /// 送信を始めようとした結果を記録する。成功なら RUNNING、失敗なら STOPPED のまま原因を残す。
    void record_start(const common::Result<void, control_protocol::ExternalOutputResult>& result);

    /// 接続テストを始めようとした結果を記録する。成功なら TESTING、失敗なら STOPPED のまま
    /// 原因を残す。
    void record_test_start(
        const common::Result<void, control_protocol::ExternalOutputResult>& result);

    /// 送信か接続テストを止めたことを記録する。
    void record_stop();

    /// 接続テストが終わった結果を記録する。
    /// @return 接続テスト中でなければ（止めた後に届いた古い結果）何もせず false
    bool finish_test(control_protocol::ExternalOutputResult result);

private:
    control_protocol::ExternalOutput info_;
    std::optional<transport::SrtpMasterKey> key_;
};

/// 外部出力のプロファイルのチャンネル数を返す。
[[nodiscard]] std::size_t channels_of(control_protocol::ExternalOutputProfile profile) noexcept;

/// control-protocol の暗号スイートを transport の暗号スイートに写す。
[[nodiscard]] transport::SrtpCryptoSuite to_transport_suite(
    control_protocol::SrtpSuite suite) noexcept;

}  // namespace vocroz::session
