// SPDX-License-Identifier: AGPL-3.0-only
//
// HttpsRendezvousClient の実装。要求はキューに積み、作業用のスレッドが順に送る。
// 順に送るのは、親機の「発行し直し」で古い招待の失効と新しい招待の登録が続くとき、
// サーバーに届く順序を呼ばれた順序と同じにするため。

#include "vocroz/network/https_rendezvous_client.h"

#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <utility>

#include "network_constants.h"
#include "wire/invite_api_codec.h"

namespace vocroz::network {
namespace {

using control_protocol::ErrorCode;

}  // namespace

struct HttpsRendezvousClient::Impl {
    Impl(HttpsTransport& transport_in, InviteResultListener& listener_in)
        : transport(transport_in), listener(listener_in), worker([this] { run(); }) {}

    ~Impl() {
        {
            const std::lock_guard lock(mutex);
            stopping = true;
        }
        wake.notify_one();
        worker.join();
    }

    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;

    void enqueue(std::function<void()> job) {
        {
            const std::lock_guard lock(mutex);
            jobs.push_back(std::move(job));
        }
        wake.notify_one();
    }

    void run() {
        for (;;) {
            std::function<void()> job;
            {
                std::unique_lock lock(mutex);
                wake.wait(lock, [this] { return stopping || !jobs.empty(); });
                // 止めるときも、残った要求（特に失効）を送り終えてから抜ける。
                if (jobs.empty()) {
                    return;
                }
                job = std::move(jobs.front());
                jobs.pop_front();
            }
            job();
        }
    }

    /// POST して応答を返す。サーバーに届かなければ（名前解決、接続、TLS、タイムアウトの失敗）
    /// 応答の代わりに kUnreachable を返す。
    common::Result<HttpResponse, ErrorCode> send(std::string_view path, const std::string& body) {
        auto response = transport.post_json(path, body);
        if (!response) {
            return common::make_unexpected(ErrorCode::kUnreachable);
        }
        return std::move(response).value();
    }

    void do_register(const session::InviteRegistration& registration) {
        const auto response =
            send(kRegisterInvitePath, wire::encode_register_request(registration));
        if (!response) {
            listener.on_invite_registered(registration.invite_token,
                                          common::make_unexpected(response.error()));
            return;
        }
        listener.on_invite_registered(registration.invite_token,
                                      wire::decode_register_response(response.value()));
    }

    void do_revoke(const session::InviteCode& code, const session::RevokeKey& revoke_key) {
        // 失効の結果は使わない。失敗しても、親機は失効させた招待での参加を拒否し、
        // サーバー側の招待も有効期限で消える。
        static_cast<void>(send(kRevokeInvitePath, wire::encode_revoke_request(code, revoke_key)));
    }

    void do_resolve(const session::InviteCode& code) {
        const auto response = send(kResolveInvitePath, wire::encode_resolve_request(code));
        if (!response) {
            listener.on_invite_resolved(code, common::make_unexpected(response.error()));
            return;
        }
        listener.on_invite_resolved(code, wire::decode_resolve_response(code, response.value()));
    }

    HttpsTransport& transport;
    InviteResultListener& listener;
    std::mutex mutex;
    std::condition_variable wake;
    std::deque<std::function<void()>> jobs;
    bool stopping = false;
    // 他のメンバーを初期化してから起動し、破棄では最初に止めるので最後に宣言する。
    std::thread worker;
};

HttpsRendezvousClient::HttpsRendezvousClient(HttpsTransport& transport,
                                             InviteResultListener& listener)
    : impl_(std::make_unique<Impl>(transport, listener)) {}

HttpsRendezvousClient::~HttpsRendezvousClient() = default;

void HttpsRendezvousClient::register_invite(const session::InviteRegistration& registration) {
    impl_->enqueue([impl = impl_.get(), registration] { impl->do_register(registration); });
}

void HttpsRendezvousClient::revoke_invite(const session::InviteCode& code,
                                          const session::RevokeKey& revoke_key) {
    impl_->enqueue([impl = impl_.get(), code, revoke_key] { impl->do_revoke(code, revoke_key); });
}

void HttpsRendezvousClient::resolve_invite(const session::InviteCode& code) {
    impl_->enqueue([impl = impl_.get(), code] { impl->do_resolve(code); });
}

}  // namespace vocroz::network
