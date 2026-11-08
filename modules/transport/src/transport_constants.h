// SPDX-License-Identifier: AGPL-3.0-only
//
// transport の内部だけで使う定数。
#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace vocroz::transport {

/// ワイヤーフォーマットのバージョン。形式を変えるときは値を上げ、古い値のパケットは捨てる。
inline constexpr std::uint8_t kProtocolVersion = 1;

/// 使う Noise プロトコル（docs/adr/0001-key-exchange-noise-ik.md）。
inline constexpr const char* kNoiseProtocolName = "Noise_IK_25519_ChaChaPoly_BLAKE2s";

/// Noise のプロローグの先頭に置くラベル。後ろにバージョンと接続 ID を続ける。
/// プロローグはハンドシェイクのハッシュに混ぜられるので、別の製品やバージョンとの取り違えと、
/// 平文のヘッダーにある接続 ID の書き換えを、ハンドシェイクの失敗として検出できる。
inline constexpr std::string_view kPrologueLabel = "VOCROZ transport";

/// ChaCha20-Poly1305（IETF）の nonce の長さ（バイト）。
inline constexpr std::size_t kAeadNonceBytes = 12;

/// Noise のハンドシェイクメッセージの一時公開鍵の長さ（バイト）。
inline constexpr std::size_t kEphemeralKeyBytes = 32;

// ---- 信頼制御層（docs/adr/0019-reliable-control-layer.md） ----

/// 制御パケットのフレーム種別: ACK。
inline constexpr std::uint8_t kAckFrameType = 1;

/// 制御パケットのフレーム種別: データ（制御メッセージ 1 つ）。
inline constexpr std::uint8_t kDataFrameType = 2;

/// レーンごとに、ACK を待たずに送ってよいメッセージの数。
/// 選択 ACK のビット列（64 ビット）で受信状況を 1 回に伝えきれる幅にそろえる。
/// 制御メッセージは操作 1 回に 1 つ程度なので、MIDI の連打でも 64 は十分に大きい。
inline constexpr std::uint32_t kReliableWindowMessages = 64;

/// レーンごとに、送信待ちとして保持するメッセージの上限。
/// 相手が ACK を返さないまま送り続けたときに、メモリを使い続けないための上限。
/// 窓の 4 倍あれば、一時的な輻輳で窓が詰まっても操作を取りこぼさない。
inline constexpr std::size_t kMaxQueuedMessages = 256;

/// RTT の測定値がまだないときの再送タイムアウト。
/// インターネット越しの RTT（数十〜200ms 程度）を上回り、最初の欠落からの回復を遅らせすぎない値。
inline constexpr std::chrono::milliseconds kInitialRetransmitTimeout{250};

/// 再送タイムアウトの下限。
/// TCP の 1 秒（RFC 6298）は合奏中のミュート通知には遅すぎるので、LAN の RTT でも
/// 不要な再送が続かない程度まで下げる。
inline constexpr std::chrono::milliseconds kMinRetransmitTimeout{50};

/// 再送タイムアウトの上限。再送のたびに倍にしても、受信の途絶の判定（5 秒）までに
/// 数回は再送できるようにする。
inline constexpr std::chrono::milliseconds kMaxRetransmitTimeout{1000};

/// 送信が途絶えたときに、キープアライブの制御パケットを送る間隔。
/// NAT のマッピングを保ち、相手が受信の途絶を判定できるようにする。
inline constexpr std::chrono::seconds kKeepaliveInterval{1};

/// 音声と制御のどちらも届かなければ、受信が途絶したとみなす時間。
/// キープアライブ 5 回分で、1 つや 2 つの欠落では途絶と判定しない。
inline constexpr std::chrono::seconds kPeerTimeout{5};

}  // namespace vocroz::transport
