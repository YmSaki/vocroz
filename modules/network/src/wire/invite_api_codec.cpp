// SPDX-License-Identifier: AGPL-3.0-only
//
// 招待 API の JSON の組み立てと応答の検証の実装。nlohmann/json は例外を送出しない形
// （parse の allow_exceptions = false と、型を確かめてからの参照）だけで使う。

#include "wire/invite_api_codec.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <optional>
#include <span>
#include <string_view>
#include <tuple>
#include <utility>

#include <nlohmann/json.hpp>

#include "wire/base64url.h"

namespace vocroz::network::wire {
namespace {

using control_protocol::ErrorCode;
using nlohmann::json;

// HTTP の状態コード。
constexpr int kHttpOk = 200;
constexpr int kHttpCreated = 201;
constexpr int kHttpServerErrorBegin = 500;

/// 成功の状態コードなのに本文が正しくない応答の扱い。サーバーが壊れた応答を返すのは、
/// サーバーが正しく動いていないときなので、届かないときと同じ対処（時間を置いて再試行する）を促す。
constexpr ErrorCode kMalformedResponse = ErrorCode::kUnreachable;

/// 本文を JSON のオブジェクトとして読む。オブジェクトでなければ std::nullopt。
std::optional<json> parse_object(const std::string& body) {
    json j = json::parse(body, nullptr, /*allow_exceptions=*/false);
    if (!j.is_object()) {
        return std::nullopt;
    }
    return j;
}

/// オブジェクトの文字列のフィールドを返す。ないか文字列でなければ std::nullopt。
std::optional<std::string_view> string_field(const json& object, const char* key) {
    const auto it = object.find(key);
    if (it == object.end() || !it->is_string()) {
        return std::nullopt;
    }
    return std::string_view(it->get_ref<const std::string&>());
}

/// base64url のフィールドを、ちょうど N バイトとして読む。
template <std::size_t N>
std::optional<std::array<std::uint8_t, N>> fixed_bytes_field(const json& object, const char* key) {
    const auto text = string_field(object, key);
    if (!text) {
        return std::nullopt;
    }
    const auto bytes = decode_base64url(*text);
    if (!bytes || bytes->size() != N) {
        return std::nullopt;
    }
    std::array<std::uint8_t, N> out{};
    std::copy(bytes->begin(), bytes->end(), out.begin());
    return out;
}

/// エラーの応答を原因別のエラーコードに変換する。
/// 本文の error が読めなければ状態コードだけで判断する（途中のプロキシが返した応答など）。
ErrorCode error_from_response(const HttpResponse& response) {
    const auto object = parse_object(response.body);
    const auto code = object ? string_field(*object, "error") : std::nullopt;
    if (code == "INVALID_CODE" || code == "INVITE_NOT_FOUND") {
        return ErrorCode::kInviteInvalid;
    }
    if (code == "INVITE_EXPIRED") {
        return ErrorCode::kInviteExpired;
    }
    if (code == "INVITE_REVOKED") {
        return ErrorCode::kInviteRevoked;
    }
    // 上限（CAPACITY_EXCEEDED）と 5xx は、時間を置けば成功しうるので、届かないときと同じに扱う。
    if (code == "CAPACITY_EXCEEDED" || response.status >= kHttpServerErrorBegin) {
        return ErrorCode::kUnreachable;
    }
    // INVALID_REQUEST などの残りの 4xx は、クライアントが正しくない要求を送ったことを意味する。
    // 利用者には対処できないので、エンジン内部のエラーとして扱う。
    return ErrorCode::kEngineError;
}

}  // namespace

std::string encode_register_request(const session::InviteRegistration& registration) {
    const json body = {
        {"host_public_key", encode_base64url(registration.host_public_key)},
        {"invite_token", encode_base64url(registration.invite_token)},
        {"rendezvous_id", encode_base64url(registration.rendezvous_id)},
    };
    return body.dump();
}

std::string encode_resolve_request(const session::InviteCode& code) {
    const json body = {{"code", code.normalized()}};
    return body.dump();
}

std::string encode_revoke_request(const session::InviteCode& code,
                                  const session::RevokeKey& revoke_key) {
    const json body = {
        {"code", code.normalized()},
        {"revoke_key", encode_base64url(revoke_key)},
    };
    return body.dump();
}

common::Result<session::RegisteredInvite, ErrorCode> decode_register_response(
    const HttpResponse& response) {
    if (response.status != kHttpCreated) {
        return common::make_unexpected(error_from_response(response));
    }
    const auto object = parse_object(response.body);
    if (!object) {
        return common::make_unexpected(kMalformedResponse);
    }
    const auto code_text = string_field(*object, "code");
    const auto revoke_key =
        fixed_bytes_field<std::tuple_size_v<session::RevokeKey>>(*object, "revoke_key");
    if (!code_text || !revoke_key) {
        return common::make_unexpected(kMalformedResponse);
    }
    auto code = session::parse_invite_code(*code_text);
    // 応答のコードは正規化済みのはず。違う表記なら、別の実装のサーバーか壊れた応答とみなす。
    if (!code || code.value().normalized() != *code_text) {
        return common::make_unexpected(kMalformedResponse);
    }
    return session::RegisteredInvite{std::move(code).value(), *revoke_key};
}

common::Result<session::ResolvedInvite, ErrorCode> decode_resolve_response(
    const session::InviteCode& requested, const HttpResponse& response) {
    if (response.status != kHttpOk) {
        return common::make_unexpected(error_from_response(response));
    }
    const auto object = parse_object(response.body);
    if (!object) {
        return common::make_unexpected(kMalformedResponse);
    }
    const auto code_text = string_field(*object, "code");
    const auto host_key =
        fixed_bytes_field<std::tuple_size_v<session::HostPublicKey>>(*object, "host_public_key");
    // 親機（session）が作る招待トークンは固定長なので、それ以外の長さは受け付けない。
    const auto token =
        fixed_bytes_field<std::tuple_size_v<session::InviteToken>>(*object, "invite_token");
    const auto rendezvous_id =
        fixed_bytes_field<std::tuple_size_v<session::RendezvousId>>(*object, "rendezvous_id");
    if (!code_text || *code_text != requested.normalized() || !host_key || !token ||
        !rendezvous_id) {
        return common::make_unexpected(kMalformedResponse);
    }
    return session::ResolvedInvite{session::HostEndpoint{*host_key, *rendezvous_id}, *token};
}

}  // namespace vocroz::network::wire
