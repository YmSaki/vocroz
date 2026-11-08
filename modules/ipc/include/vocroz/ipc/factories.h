// SPDX-License-Identifier: AGPL-3.0-only
//
// 名前付きパイプの MessageChannel を生成するファクトリー関数（Windows だけ）。
// 具象クラスは公開せず、コンポジションルートはここから MessageChannel
// として受け取る（AGENTS.md 5.1）。 パイプの ACL、なりすまし対策、切断と再接続の扱いは
// docs/adr/0020-ipc-pipe-and-webui-bridge.md。
#pragma once

#include <chrono>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

#include "vocroz/common/ports/logger.h"
#include "vocroz/common/result.h"
#include "vocroz/control_protocol/ports/message_channel.h"

namespace vocroz::ipc {

/// パイプの作成と接続の失敗の原因。
enum class PipeError {
    kAlreadyExists,  ///< 同じ名前のパイプがすでにある（同じエンジンの二重起動か、名前の先取り）
    kNotFound,  ///< 待っても接続できなかった（エンジンが起動していない、またはビジーのまま）
    kAccessDenied,     ///< ACL で拒否された（ほかのユーザーのパイプ）
    kUntrustedServer,  ///< パイプの所有者がログイン中ユーザーではない
    kSystemError,      ///< そのほかの OS のエラー
};

/// ログイン中ユーザーの、pa-engine のパイプの名前を返す。
/// @param data_dir --data-dir の絶対パス
/// @return パイプの名前。ユーザーの SID を取得できなければ kSystemError
/// スレッド安全性: どのスレッドからも呼び出し可能。
[[nodiscard]] common::Result<std::wstring, PipeError> engine_pipe_name(
    const std::filesystem::path& data_dir);

/// pa-engine 側のパイプを作り、UI の接続の待ち受けを始める。
/// 返す MessageChannel は UI の再接続にまたがって同じもので、UI が切断したら次の接続を待つ。
/// UI の切断は on_closed() で知らせない。on_closed() は、待ち受けを続けられない OS のエラーが
/// 起きたときだけ呼ぶ。UI がつながっていない間の send() は kClosed で失敗する。
/// 受信はサーバーの受信スレッドからリスナーに渡す。
/// @param name   パイプの名前（engine_pipe_name() の値）
/// @param logger 接続、切断、不正なフレームを記録するロガー。戻り値より長く生存させる
/// @return MessageChannel。同じ名前のパイプがすでにあれば kAlreadyExists
/// スレッド安全性: どのスレッドからも呼び出し可能。破棄は、リスナーを解除できるスレッドで行う。
[[nodiscard]] common::Result<std::unique_ptr<control_protocol::MessageChannel>, PipeError>
make_pipe_server(std::wstring_view name, common::Logger& logger);

/// pa-engine のパイプに接続する。
/// パイプがない（エンジンの起動中）かビジーの間は、timeout まで再試行する。
/// 返す MessageChannel は 1 つの接続を表し、切断されたら on_closed() を 1 回呼ぶ。
/// つながり直すときは、この関数でクライアントを作り直す。
/// 自分で破棄したときは on_closed() を呼ばない。
/// @param name    パイプの名前（engine_pipe_name() の値）
/// @param timeout 接続を待つ最長の時間
/// @param logger  切断と不正なフレームを記録するロガー。戻り値より長く生存させる
/// @return MessageChannel。所有者がログイン中ユーザーでないパイプには接続せず kUntrustedServer
/// スレッド安全性: どのスレッドからも呼び出し可能。timeout まで呼び出し元をブロックしうる。
[[nodiscard]] common::Result<std::unique_ptr<control_protocol::MessageChannel>, PipeError>
connect_pipe_client(std::wstring_view name, std::chrono::milliseconds timeout,
                    common::Logger& logger);

}  // namespace vocroz::ipc
