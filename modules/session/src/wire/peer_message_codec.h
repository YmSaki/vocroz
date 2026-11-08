// SPDX-License-Identifier: AGPL-3.0-only
//
// 子機と親機の間の制御メッセージのエンコードとデコード（信頼境界）。
// 子機は承認前でも親機にメッセージを送れるので、親機はここで形式を検証し、内側には検証済みの
// 型だけを渡す（AGENTS.md 6.3）。改変された子機が未知の種別（たとえば Master の操作）を送っても、
// デコードで拒否され、状態は変わらない。
//
// 形式: 先頭 1 バイトが種別、続いて種別ごとのフィールド。整数はビッグエンディアン。
// 文字列は長さ（1 バイト）と UTF-8 の本体。余分なバイトがあるメッセージは不正とする。
#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "vocroz/common/result.h"

#include "wire/peer_messages.h"

namespace vocroz::session::wire {

/// デコードできなかった理由。
enum class PeerMessageError {
    kUnknownType,  ///< この向きでは受け付けない種別
    kMalformed,    ///< 長さや値が形式に合わない
};

/// 子機から親機へのメッセージをエンコードする。
/// @param message メッセージ。文字列は検証済みであること
/// @return 制御メッセージの本文
/// スレッド安全性: どのスレッドからも呼び出し可能（状態を持たない）。
[[nodiscard]] std::vector<std::uint8_t> encode(const ClientToHostMessage& message);

/// 親機から子機へのメッセージをエンコードする。
/// @param message メッセージ。文字列は検証済みであること
/// @return 制御メッセージの本文
/// スレッド安全性: どのスレッドからも呼び出し可能（状態を持たない）。
[[nodiscard]] std::vector<std::uint8_t> encode(const HostToClientMessage& message);

/// 親機が受け取ったメッセージをデコードする。子機から親機への種別だけを受け付ける。
/// @param bytes 制御メッセージの本文
/// @return 検証済みのメッセージ。不正なら理由
/// スレッド安全性: どのスレッドからも呼び出し可能（状態を持たない）。
[[nodiscard]] common::Result<ClientToHostMessage, PeerMessageError> decode_client_to_host(
    std::span<const std::uint8_t> bytes);

/// 子機が受け取ったメッセージをデコードする。親機から子機への種別だけを受け付ける。
/// @param bytes 制御メッセージの本文
/// @return 検証済みのメッセージ。不正なら理由
/// スレッド安全性: どのスレッドからも呼び出し可能（状態を持たない）。
[[nodiscard]] common::Result<HostToClientMessage, PeerMessageError> decode_host_to_client(
    std::span<const std::uint8_t> bytes);

}  // namespace vocroz::session::wire
