// SPDX-License-Identifier: AGPL-3.0-only
//
// Winsock のソケットとイベントの RAII ラッパー（AGENTS.md 6.1）。
// 失敗の経路が多い初期化の途中でも、ハンドルを閉じ忘れないようにする。
#pragma once

#include <utility>

#include <winsock2.h>

namespace vocroz::network::win {

/// SOCKET を所有し、破棄時に closesocket() する。
class SocketHandle {
public:
    SocketHandle() noexcept = default;
    /// @param socket 所有するソケット（INVALID_SOCKET でもよい）
    explicit SocketHandle(SOCKET socket) noexcept : socket_(socket) {}
    ~SocketHandle() {
        reset();
    }
    SocketHandle(const SocketHandle&) = delete;
    SocketHandle& operator=(const SocketHandle&) = delete;
    SocketHandle(SocketHandle&& other) noexcept
        : socket_(std::exchange(other.socket_, INVALID_SOCKET)) {}
    SocketHandle& operator=(SocketHandle&& other) noexcept {
        if (this != &other) {
            reset();
            socket_ = std::exchange(other.socket_, INVALID_SOCKET);
        }
        return *this;
    }

    /// ソケットを返す。
    [[nodiscard]] SOCKET get() const noexcept {
        return socket_;
    }

    /// 有効なソケットを持っていれば true を返す。
    [[nodiscard]] bool valid() const noexcept {
        return socket_ != INVALID_SOCKET;
    }

    /// ソケットを閉じる。
    void reset() noexcept {
        if (socket_ != INVALID_SOCKET) {
            closesocket(socket_);
            socket_ = INVALID_SOCKET;
        }
    }

private:
    SOCKET socket_ = INVALID_SOCKET;
};

/// WSAEVENT を所有し、破棄時に WSACloseEvent() する。
class EventHandle {
public:
    /// 手動リセットのイベントを作る。作れなければ valid() が false になる。
    EventHandle() noexcept : event_(WSACreateEvent()) {}
    ~EventHandle() {
        if (event_ != WSA_INVALID_EVENT) {
            WSACloseEvent(event_);
        }
    }
    EventHandle(const EventHandle&) = delete;
    EventHandle& operator=(const EventHandle&) = delete;

    /// イベントを返す。
    [[nodiscard]] WSAEVENT get() const noexcept {
        return event_;
    }

    /// 有効なイベントを持っていれば true を返す。
    [[nodiscard]] bool valid() const noexcept {
        return event_ != WSA_INVALID_EVENT;
    }

private:
    WSAEVENT event_;
};

}  // namespace vocroz::network::win
