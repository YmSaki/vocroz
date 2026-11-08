// SPDX-License-Identifier: AGPL-3.0-only
//
// ランデブーメッセージ（REGISTER、REGISTERED、PUNCH）のシリアライズとパース。
// 形式の正本は
// infra/rendezvous/internal/udpapi/wire.go（AGENTS.md 4.4）で、ここはそのクライアント側の実装。
// すべての整数はビッグエンディアン。共通ヘッダーは magic "VRZ"、version 1、type、txid（8 バイト）。
// 候補アドレスは family（4 か 6）、port、address（4 か 16 バイト）。
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "vocroz/common/result.h"
#include "vocroz/network/network_error.h"
#include "vocroz/network/rendezvous_types.h"
#include "vocroz/network/udp_address.h"

namespace vocroz::network::wire {

/// トランザクション ID。REGISTER の送信者が決め、対応する REGISTERED と PUNCH にそのまま入る。
using TxId = std::array<std::uint8_t, 8>;

/// ランデブーメッセージの共通ヘッダーの長さ（バイト）。
inline constexpr std::size_t kRendezvousHeaderBytes = 13;

/// REGISTER の最大長（バイト）。候補が kMaxLocalCandidates 個すべて IPv6 のとき。
inline constexpr std::size_t kMaxRegisterBytes =
    kRendezvousHeaderBytes + 2 * kRendezvousIdBytes + 2 + kMaxLocalCandidates * 19;

/// PUNCH の最大長（バイト）。サーバーは申告された候補に再帰アドレスを 1 つ加える。
inline constexpr std::size_t kMaxPunchBytes =
    kRendezvousHeaderBytes + 2 * kRendezvousIdBytes + 2 + (kMaxLocalCandidates + 1) * 19;

/// REGISTER。親機と子機が送る。
struct RegisterMessage {
    TxId tx_id{};
    RendezvousId rendezvous_id{};
    PeerId peer_id{};
    RendezvousRole role = RendezvousRole::kClient;
    /// 申告するローカル候補。kMaxLocalCandidates 個以下。
    std::span<const UdpAddress> candidates;
};

/// REGISTERED。サーバーが REGISTER の送信者に返す。
struct RegisteredMessage {
    TxId tx_id{};
    /// サーバーの上限に達していて登録できなかった。
    bool capacity_exceeded = false;
    /// 相手の役割の登録がすでにある。
    bool peer_present = false;
    /// サーバーから見た送信者のアドレス。
    UdpAddress reflexive;
};

/// PUNCH。サーバーが親機と子機の双方に同時に送り、ホールパンチの開始を知らせる。
struct PunchMessage {
    /// 登録者が受け取る PUNCH は自分の REGISTER の txid、相手が受け取る PUNCH は 0。
    TxId tx_id{};
    RendezvousId rendezvous_id{};
    /// 相手のピア ID。
    PeerId peer_id{};
    /// 相手の役割。
    RendezvousRole peer_role = RendezvousRole::kHost;
    /// 相手の候補。サーバー再帰アドレスが先頭に来る。
    std::vector<UdpAddress> candidates;
};

/// データグラムがランデブーメッセージの形（先頭が "VRZ" でヘッダーの長さ以上）をしていれば true
/// を返す。 transport のパケットの先頭はプロトコルバージョン（1）なので衝突しない。
/// @param datagram 受信したデータグラム
/// スレッド安全性: 純粋関数で、どのスレッドからも呼び出し可能。ヒープを確保しない。
[[nodiscard]] bool is_rendezvous_message(std::span<const std::uint8_t> datagram) noexcept;

/// REGISTER をシリアライズする。
/// @param message 送る REGISTER
/// @param out     書き込み先。kMaxRegisterBytes バイトあれば足りる
/// @return 書き込んだバイト数。候補が kMaxLocalCandidates 個を超えるか、送れない候補
///         （is_routable_candidate() が false）を含むか、out が小さければ kSendFailed
/// スレッド安全性: 純粋関数で、どのスレッドからも呼び出し可能。
[[nodiscard]] common::Result<std::size_t, NetworkError> encode_register(
    const RegisterMessage& message, std::span<std::uint8_t> out) noexcept;

/// REGISTERED をパースして検証する。
/// @param datagram 受信したデータグラム
/// @return 検証済みの REGISTERED。形式、バージョン、種別、状態の値、候補のどれかが不正なら
/// kMalformedResponse スレッド安全性: 純粋関数で、どのスレッドからも呼び出し可能。
[[nodiscard]] common::Result<RegisteredMessage, NetworkError> parse_registered(
    std::span<const std::uint8_t> datagram) noexcept;

/// PUNCH をパースして検証する。
/// @param datagram 受信したデータグラム
/// @return 検証済みの PUNCH。形式、バージョン、種別、役割、候補の数と値のどれかが不正なら
/// kMalformedResponse スレッド安全性: 純粋関数で、どのスレッドからも呼び出し可能。
[[nodiscard]] common::Result<PunchMessage, NetworkError> parse_punch(
    std::span<const std::uint8_t> datagram);

}  // namespace vocroz::network::wire
