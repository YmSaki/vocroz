// SPDX-License-Identifier: AGPL-3.0-only
//
// UI とエンジン間のメッセージの、どの entity にも共通する上限と範囲。
// 名前付きパイプ（ipc）と WebView のブリッジ（webui）のフレーミングも、同じ上限を使う。
#pragma once

#include <cstddef>
#include <cstdint>

namespace vocroz::control_protocol {

/// 1 メッセージの JSON の最大バイト数。
/// 最大のメッセージは親機のスナップショットで、ストリップ 64 本でも 20 KiB 程度に収まる。
/// 余裕を見て 64 KiB
/// とし、これを超える入力は不正として読まずに拒否する（巨大な入力でメモリを使わせないため）。
inline constexpr std::size_t kMaxMessageBytes = std::size_t{64} * 1024;

/// ゲインの有限の最小値（dB）。ミキサーのフェーダーと子機の送信レベルの下限はどちらも -60 dB で、
/// それより下のフェーダーは -∞ dB（JSON では null）になる。
inline constexpr double kMinFiniteGainDb = -60.0;

/// ゲインの最大値（dB）。ミキサーのフェーダーは +10 dB、子機の送信レベルは +12 dB までで、
/// 大きいほうに合わせる。entity ごとの上限は session が判断する。
inline constexpr double kMaxGainDb = 12.0;

/// ルーティングの遅延（手動ディレイと遅延ノード）の最大値（ms）。遅延ノードの上限（2
/// 秒）に合わせる。 entity ごとの範囲は session が判断する。
inline constexpr double kMaxRoutingDelayMs = 2000.0;

/// 全体の目標レイテンシー（ジッターバッファの深さの上限、ms）の範囲。
/// 下限は低遅延プロファイルの既定の下限（10ms、docs/adr/0002-opus-low-delay.md）、
/// 上限はジッターバッファが受け付ける最大値（250ms）に合わせる。
inline constexpr double kMinLatencyTargetMs = 10.0;
inline constexpr double kMaxLatencyTargetMs = 250.0;

/// 1 台の子機が送れる入力（ストリーム）の数の上限。親機では 1 本ずつ別のストリップになる。
/// マイク 2 本とアプリ音声のような使い方に足り、子機の CPU（ストリームごとの Opus のエンコード）と
/// 回線（1 本あたり
/// 128〜192kbps）の負荷を抑えられる値にする（docs/adr/0035-client-input-expansion.md）。
inline constexpr std::size_t kMaxInputStreams = 4;

/// 1 つの入力のチャンネル数の上限（ステレオ）。
inline constexpr std::size_t kMaxInputChannels = 2;

/// SRTP 外部出力の RTP のペイロードタイプの範囲（RFC 3551 の動的な範囲）と既定値。
/// 既定の 111 は WebRTC など多くの実装が Opus に使う値で、受信機の SDP と合わせやすい
/// （docs/adr/0040-srtp-external-output.md）。transport の kMinDynamicPayloadType と同じ値にする。
inline constexpr std::uint8_t kMinExternalOutputPayloadType = 96;
inline constexpr std::uint8_t kMaxExternalOutputPayloadType = 127;
inline constexpr std::uint8_t kDefaultExternalOutputPayloadType = 111;

/// SRTP 外部出力の宛先のポートの既定値。RTP/AVP の慣例的なポート（RFC 3551）。
inline constexpr std::uint16_t kDefaultExternalOutputPort = 5004;

/// SRTP 外部出力の宛先のホスト名の最大バイト数（DNS の名前の上限）。
inline constexpr std::size_t kMaxExternalOutputHostBytes = 253;

/// SRTP 外部出力の鍵の文字列の最大バイト数。30 バイトの Base64（40 文字）に "inline:" を付けた
/// 長さに、前後の空白の余裕を足す。形式の検証は session が行う。
inline constexpr std::size_t kMaxSrtpKeyBytes = 64;

/// プラグインの識別子の上限（バイト）。JUCE の識別子は「形式-名前-ファイルのハッシュ-ID」で、
/// 名前の長さで変わる（多くは 60 バイト前後）。長い製品名でも収まり、挿入の上限（32）まで並べても
/// スナップショットが kMaxMessageBytes
/// に収まる値にする。この長さを超えるプラグインは一覧に載せない。
inline constexpr std::size_t kMaxPluginIdBytes = 256;

/// 参加者名、セッション名、デバイス名、短い識別 ID、バージョン、プラグインの名前とファイルの
/// パスなど、表示用の文字列の最大バイト数。
inline constexpr std::size_t kMaxTextBytes = 512;

/// プラグインの一覧のページ（plugin_catalog_page）1 つに載せる項目の上限。
/// 1 項目は識別子（256 バイト）と名前、製造元、バージョン（512 バイトずつ）で最大 2 KiB
/// 程度なので、 すべての項目が上限の長さでも kMaxMessageBytes
/// に収まる数にする。プラグインの一覧は数百件に
/// なるので、スナップショットには入れず、ページに分けて送る（docs/adr/0042-plugin-insert.md）。
inline constexpr std::size_t kMaxPluginCatalogPageEntries = 24;

/// プラグインの状態（plugins）に載せる、読み込み不可のファイルの上限。パスは kMaxTextBytes まで
/// なので、挿入の状態（32 件）と合わせても 1 メッセージに収まる数にする。
inline constexpr std::size_t kMaxBlockedPluginFiles = 32;

/// revision とシーケンス番号の最大値。UI（JavaScript）の数値で正確に表せる最大の整数（2^53 - 1）。
inline constexpr std::uint64_t kMaxCounter = (std::uint64_t{1} << 53) - 1;

}  // namespace vocroz::control_protocol
