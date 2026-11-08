// SPDX-License-Identifier: AGPL-3.0-only
//
// 名前付きパイプの上のフレーミング（長さ付き JSON）。
// 1 フレームは本文のバイト数を表す 4 バイトのリトルエンディアンの符号なし整数と、本文からなる
// （docs/adr/0020-ipc-pipe-and-webui-bridge.md）。OS に依存しないので、Linux の CI でも検証する。
#pragma once

#include <cstddef>
#include <functional>
#include <span>
#include <string>
#include <string_view>

#include "vocroz/common/result.h"

namespace vocroz::ipc {

/// 長さの前置きのバイト数。
inline constexpr std::size_t kFrameHeaderBytes = 4;

/// フレームの不正の原因。
enum class FrameError {
    kTooLarge,  ///< 本文が control_protocol::kMaxMessageBytes を超えている
};

/// 1 メッセージをフレームにする。
/// @param message 本文（1 メッセージ分の JSON）
/// @return 長さの前置きと本文をつなげたバイト列。本文が上限を超えていれば kTooLarge
/// スレッド安全性: 状態を持たないので、どのスレッドからも同時に呼び出し可能。
[[nodiscard]] common::Result<std::string, FrameError> encode_frame(std::string_view message);

/// 受信したバイト列からフレームを取り出す。
/// パイプの読み込みは任意の位置で切れるので、途中までのフレームを内部に保持して続きを待つ。
/// スレッド安全性: 1 つのオブジェクトを 1 つのスレッド（受信スレッド）からだけ使う。
class FrameDecoder {
public:
    /// 本文を受け取る関数。本文は呼び出しから戻った後は参照されない。
    using MessageSink = std::function<void(std::string_view message)>;

    /// 受信したバイト列を足し、完成したフレームの本文ごとに on_message を呼ぶ。
    /// @param bytes      受信したバイト列
    /// @param on_message 本文を受け取る関数
    /// @return 長さが上限を超えるフレームがあれば kTooLarge。本文は読まずに捨てる。
    ///         以降の境界は信用できないので、呼び出し側は接続を切る。
    ///         失敗した後の feed() は何もせずに同じエラーを返す
    [[nodiscard]] common::Result<void, FrameError> feed(std::span<const std::byte> bytes,
                                                        const MessageSink& on_message);

    /// 途中までのフレームとして保持しているバイト数。
    [[nodiscard]] std::size_t buffered_bytes() const noexcept {
        return buffer_.size();
    }

private:
    std::string buffer_;
    bool failed_ = false;
};

}  // namespace vocroz::ipc
