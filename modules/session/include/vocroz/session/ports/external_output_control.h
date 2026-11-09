// SPDX-License-Identifier: AGPL-3.0-only
//
// SRTP 外部出力の送信を始めて止めるポート。pa-engine が、送り出すバスのストリームタップ、
// Opus のエンコード、送信スレッド、宛先ごとの UDP ソケットを組み立てて実装する
// （docs/adr/0040-srtp-external-output.md）。
// session は、いつ送るか（オペレーターの操作、鍵と宛先がそろっているか）だけを決める。
// 鍵は検証済みの transport::SrtpMasterKey で渡し、鍵がなければ呼べない形にする（平文を送らない）。
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "vocroz/common/result.h"
#include "vocroz/control_protocol/states.h"
#include "vocroz/transport/external_stream_sender.h"
#include "vocroz/transport/srtp_master_key.h"

namespace vocroz::session {

/// 外部出力の送り方。session が設定から組み立てる。
struct ExternalOutputPlan {
    /// 宛先のホスト名か IP アドレス（空にしない）。
    std::string host;
    /// 宛先の UDP ポート。
    std::uint16_t port = 0;
    /// 送り出すバスの entity_id（Master か、適用中のルーティングのバス）。
    std::string source_id;
    /// エンコードのチャンネル数（1 か 2）。
    std::size_t channels = 2;
    /// RTP のペイロードタイプ（96〜127）。
    std::uint8_t payload_type = 0;
    transport::SrtpCryptoSuite suite = transport::SrtpCryptoSuite::kAesCm128HmacSha1_80;

    friend bool operator==(const ExternalOutputPlan&, const ExternalOutputPlan&) = default;
};

/// 外部出力の送信。
/// スレッド安全性: コントロールスレッドからだけ呼ぶ。名前解決とソケットを開く処理でブロックしうる。
class ExternalOutputControl {
public:
    virtual ~ExternalOutputControl() = default;

    /// 送り出すバスの音を送り始める。送信中なら止めてから始め直す。
    /// @param plan 送り方
    /// @param key  鍵
    /// @return 始められなければ原因（kResolveFailed、kSendFailed）。そのときは何も送らない
    [[nodiscard]] virtual common::Result<void, control_protocol::ExternalOutputResult> start(
        const ExternalOutputPlan& plan, const transport::SrtpMasterKey& key) = 0;

    /// 送信と接続テストを止める。止まっていれば何もしない。
    virtual void stop() = 0;

    /// 接続テストを始める。同じ SRTP の経路で試験音を数秒送り、終わったら結果を
    /// HostSession::on_external_output_test_finished() で知らせる（コンポジションルートが
    /// コントロールスレッドへ移して渡す）。
    /// @param plan 送り方（source_id は使わない）
    /// @param key  鍵
    /// @return 始められなければ原因。そのときは結果を知らせない
    [[nodiscard]] virtual common::Result<void, control_protocol::ExternalOutputResult> start_test(
        const ExternalOutputPlan& plan, const transport::SrtpMasterKey& key) = 0;
};

}  // namespace vocroz::session
