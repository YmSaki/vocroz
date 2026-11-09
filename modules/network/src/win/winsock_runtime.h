// SPDX-License-Identifier: AGPL-3.0-only
//
// Winsock の初期化（WSAStartup）と後始末（WSACleanup）の RAII ラッパー（AGENTS.md 6.1）。
// Winsock は呼び出しの回数を数えて管理するので、ソケットを使うオブジェクトがそれぞれ 1
// つ持てばよい。
#pragma once

namespace vocroz::network::win {

/// Winsock 2.2 の初期化を保持する。
/// スレッド安全性: 生成と破棄はどのスレッドからでもよい。
class WinsockRuntime {
public:
    WinsockRuntime() noexcept;
    ~WinsockRuntime();
    WinsockRuntime(const WinsockRuntime&) = delete;
    WinsockRuntime& operator=(const WinsockRuntime&) = delete;

    /// 初期化に成功していれば true を返す。
    [[nodiscard]] bool ok() const noexcept {
        return ok_;
    }

private:
    bool ok_ = false;
};

}  // namespace vocroz::network::win
