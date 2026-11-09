// SPDX-License-Identifier: AGPL-3.0-only
//
// Winsock の初期化と後始末。

#include "win/winsock_runtime.h"

#include <winsock2.h>

namespace vocroz::network::win {

WinsockRuntime::WinsockRuntime() noexcept {
    WSADATA data{};
    ok_ = WSAStartup(MAKEWORD(2, 2), &data) == 0;
}

WinsockRuntime::~WinsockRuntime() {
    if (ok_) {
        WSACleanup();
    }
}

}  // namespace vocroz::network::win
