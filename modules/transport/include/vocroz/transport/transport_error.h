// SPDX-License-Identifier: AGPL-3.0-only
//
// transport が返すエラーの原因。
// パケットの破棄理由をエラーコードで区別し、呼び出し側が統計やログに使えるようにする
// （AGENTS.md 6.2）。どの理由でも、破棄したパケットは音声とセッション状態に影響させない。
#pragma once

namespace vocroz::transport {

/// transport の処理が失敗した原因。
enum class TransportError {
    /// パケットが固定ヘッダーや認証タグより短い（切り詰められている）。
    kTruncated,
    /// ヘッダーのプロトコルバージョンが対応していない値。
    kUnsupportedVersion,
    /// ヘッダーのパケット種別が定義されていない値。
    kUnknownPacketType,
    /// 定義済みだが、その処理では受け付けない種別。
    /// 例: 暗号化済みパケットの受信にハンドシェイクのパケットを渡した。
    kUnexpectedPacketType,
    /// ヘッダーの接続 ID が、この接続のものと違う。
    kConnectionMismatch,
    /// AEAD の認証に失敗した（改ざん、鍵の不一致、偽造）。
    kAuthenticationFailed,
    /// 受信済みか、リプレイ排除の窓より古いシーケンス番号。
    kReplayed,
    /// Noise のハンドシェイクが失敗した（相手の鍵の不一致、メッセージの改ざんなど）。
    kHandshakeFailed,
    /// 呼び出しの順序が誤っている（例: ハンドシェイクの完了前に finish() を呼んだ）。
    kInvalidState,
    /// 出力先のバッファが小さすぎる。
    kBufferTooSmall,
    /// 平文がパケットの最大長に収まらない。
    kPayloadTooLarge,
    /// 送信のシーケンス番号を使い切った。同じ nonce を再利用しないよう、それ以上は送信しない。
    kNonceExhausted,
    /// 認証に成功したが、復号した中身の形式が不正。
    kMalformedPayload,
    /// 暗号ライブラリの初期化に失敗した。
    kCryptoUnavailable,
    /// ソケットからデータグラムを送れなかった。
    kSendFailed,
    /// 信頼制御層の送信待ちのメッセージが上限に達した。相手が ACK を返していない。
    kQueueFull,
    /// 信頼制御層のレーンの連番を使い切った。接続を張り直すまで、そのレーンでは送れない。
    kSequenceExhausted,
    /// 保存しておいた鍵の長さか値が不正（StaticKeyPair::from_private_key()）。
    kInvalidKey,
};

}  // namespace vocroz::transport
