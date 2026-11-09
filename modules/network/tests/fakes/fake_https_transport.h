// SPDX-License-Identifier: AGPL-3.0-only
//
// テスト用の HttpsTransport。送られた要求を記録し、テストが積んだ応答を順に返す。
// HttpsRendezvousClient の作業用のスレッドから呼ばれるので、記録と応答はロックで守る。
#pragma once

#include <deque>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include "vocroz/network/ports/https_transport.h"

namespace vocroz::network {

/// 積んだ応答を順に返す HttpsTransport。応答が尽きたら kTimedOut を返す。
class FakeHttpsTransport final : public HttpsTransport {
public:
    /// 記録した要求。
    struct Request {
        std::string path;
        std::string body;
    };

    [[nodiscard]] common::Result<HttpResponse, NetworkError> post_json(
        std::string_view path, std::string_view body) override {
        const std::lock_guard lock(mutex_);
        requests_.push_back(Request{std::string(path), std::string(body)});
        if (responses_.empty()) {
            return common::make_unexpected(NetworkError::kTimedOut);
        }
        auto next = std::move(responses_.front());
        responses_.pop_front();
        return next;
    }

    /// 次の要求に返す応答を積む。
    void push_response(int status, std::string body) {
        const std::lock_guard lock(mutex_);
        responses_.emplace_back(HttpResponse{status, std::move(body)});
    }

    /// 次の要求に返す失敗を積む。
    void push_failure(NetworkError error) {
        const std::lock_guard lock(mutex_);
        responses_.emplace_back(common::make_unexpected(error));
    }

    /// これまでに記録した要求の写し。
    [[nodiscard]] std::vector<Request> requests() const {
        const std::lock_guard lock(mutex_);
        return requests_;
    }

private:
    mutable std::mutex mutex_;
    std::vector<Request> requests_;
    std::deque<common::Result<HttpResponse, NetworkError>> responses_;
};

}  // namespace vocroz::network
