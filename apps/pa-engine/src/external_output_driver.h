// SPDX-License-Identifier: AGPL-3.0-only
//
// session の ExternalOutputControl の実装。SRTP 外部出力の部品（宛先ごとの UDP ソケット、
// RTP と SRTP の送信、Opus のエンコード、送信スレッド、送り出すバスのストリームタップ）を
// 組み立てて、送信と接続テストを始めて止める（docs/adr/0040-srtp-external-output.md）。
//
// 送信の部品は検証済みの鍵がないと作れない（ExternalStreamSender::create()）ので、
// ここでも鍵なしに送る経路はない。部品の組み立てに 1 つでも失敗したら何も送らない。
#pragma once

#include <chrono>
#include <functional>
#include <memory>

#include "vocroz/session/ports/external_output_control.h"
#include "vocroz/transport/ports/random_source.h"

#include "engine_mixer.h"
#include "external_socket_opener.h"
#include "external_stream_worker.h"

namespace vocroz::engine {

/// 接続テストで試験音を送る長さ。受信機のメーターで確かめられ、待たせすぎない長さにする。
inline constexpr std::chrono::milliseconds kExternalOutputTestDuration{3000};

/// SRTP 外部出力の送信。
/// スレッド安全性: すべてのメンバー関数をコントロールスレッドから呼ぶ。
class ExternalOutputDriver final : public session::ExternalOutputControl {
public:
    /// 接続テストの結果を渡す関数。送信スレッドから呼ばれるので、コンポジションルートは
    /// コントロールスレッドへ移してから session に渡す。
    using TestFinished = ExternalStreamWorker::TestFinished;

    /// @param mixer       ストリームタップをつなぐミキサー。このオブジェクトより長く生存させる
    /// @param sockets     宛先のソケットを開くもの。このオブジェクトより長く生存させる
    /// @param random      SSRC と RTP の初期値の乱数。このオブジェクトより長く生存させる
    /// @param on_test     接続テストの結果の渡し先
    /// @param test_length 接続テストで試験音を送る長さ（テストで短くする）
    ExternalOutputDriver(EngineMixer& mixer, ExternalSocketOpener& sockets,
                         transport::RandomSource& random, TestFinished on_test,
                         std::chrono::milliseconds test_length = kExternalOutputTestDuration);
    ExternalOutputDriver(const ExternalOutputDriver&) = delete;
    ExternalOutputDriver& operator=(const ExternalOutputDriver&) = delete;
    ~ExternalOutputDriver() override;

    // --- session::ExternalOutputControl ---
    [[nodiscard]] common::Result<void, control_protocol::ExternalOutputResult> start(
        const session::ExternalOutputPlan& plan, const transport::SrtpMasterKey& key) override;
    void stop() override;
    [[nodiscard]] common::Result<void, control_protocol::ExternalOutputResult> start_test(
        const session::ExternalOutputPlan& plan, const transport::SrtpMasterKey& key) override;

private:
    // 送信の部品を組み立てる。失敗したら何も残さない。
    [[nodiscard]] common::Result<void, control_protocol::ExternalOutputResult> assemble(
        const session::ExternalOutputPlan& plan, const transport::SrtpMasterKey& key);

    EngineMixer& mixer_;
    ExternalSocketOpener& sockets_;
    transport::RandomSource& random_;
    TestFinished on_test_;
    std::chrono::milliseconds test_length_;
    std::shared_ptr<ExternalStreamPipe> pipe_;
    std::unique_ptr<ExternalStreamWorker> worker_;
};

}  // namespace vocroz::engine
