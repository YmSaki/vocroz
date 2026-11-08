// SPDX-License-Identifier: AGPL-3.0-only
//
// factories.h の実装。具象クラスを MessageChannel として返す。

#include <filesystem>
#include <system_error>

#include "vocroz/ipc/factories.h"
#include "vocroz/ipc/pipe_name.h"

#include "win/named_pipe_client.h"
#include "win/named_pipe_server.h"
#include "win/win32_security.h"
#include "win/win32_text.h"

namespace vocroz::ipc {

using control_protocol::MessageChannel;

common::Result<std::wstring, PipeError> engine_pipe_name(const std::filesystem::path& data_dir) {
    const auto sid = win::current_user_sid();
    if (!sid) {
        return common::make_unexpected(PipeError::kSystemError);
    }
    // 相対パスや `..` を含む表記でも、同じフォルダなら同じ名前になるようにそろえる。
    std::error_code error;
    const auto absolute = std::filesystem::absolute(data_dir, error).lexically_normal();
    if (error) {
        return common::make_unexpected(PipeError::kSystemError);
    }
    const std::string name =
        make_engine_pipe_name(win::to_utf8(*sid), win::to_utf8(absolute.native()));
    return win::to_wide(name);
}

common::Result<std::unique_ptr<MessageChannel>, PipeError> make_pipe_server(
    std::wstring_view name, common::Logger& logger) {
    auto server = win::NamedPipeServer::create(std::wstring{name}, logger);
    if (!server) {
        return common::make_unexpected(server.error());
    }
    return std::unique_ptr<MessageChannel>{std::move(server).value()};
}

common::Result<std::unique_ptr<MessageChannel>, PipeError> connect_pipe_client(
    std::wstring_view name, std::chrono::milliseconds timeout, common::Logger& logger) {
    auto client = win::NamedPipeClient::connect(std::wstring{name}, timeout, logger);
    if (!client) {
        return common::make_unexpected(client.error());
    }
    return std::unique_ptr<MessageChannel>{std::move(client).value()};
}

}  // namespace vocroz::ipc
