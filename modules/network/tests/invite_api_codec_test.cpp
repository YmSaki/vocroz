// SPDX-License-Identifier: AGPL-3.0-only
//
// 招待 API の JSON の組み立てと応答の検証のテスト。要求と応答の形は
// infra/rendezvous/internal/httpapi/invites.go の DTO に合わせている。

#include "wire/invite_api_codec.h"

#include <string>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

namespace vocroz::network::wire {
namespace {

using control_protocol::ErrorCode;
using nlohmann::json;

// 0x00, 0x01, ..., 0x1F を base64url にしたもの。
constexpr const char* kSequential32 = "AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8";
// 0xAA が 32 バイト。
constexpr const char* kAa32 = "qqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqo";
// 0x11 が 16 バイト。
constexpr const char* kEleven16 = "EREREREREREREREREREREQ";

session::InviteCode code_of(std::string_view text) {
    auto code = session::parse_invite_code(text);
    EXPECT_TRUE(code.has_value());
    return std::move(code).value();
}

session::HostPublicKey sequential_key() {
    session::HostPublicKey key{};
    for (std::size_t i = 0; i < key.size(); ++i) {
        key[i] = static_cast<std::uint8_t>(i);
    }
    return key;
}

template <typename Array>
Array filled(std::uint8_t value) {
    Array out{};
    out.fill(value);
    return out;
}

std::string resolve_body(const char* code, const char* host_key, const char* token,
                         const char* rendezvous_id) {
    return json{{"code", code},
                {"host_public_key", host_key},
                {"invite_token", token},
                {"rendezvous_id", rendezvous_id},
                {"expires_at", "2026-11-08T12:30:00Z"}}
        .dump();
}

std::string error_body(const char* code) {
    return json{{"error", code}}.dump();
}

TEST(InviteApiCodecTest, RegisterRequestCarriesTheKeysAsBase64Url) {
    const session::InviteRegistration registration{
        sequential_key(), filled<session::InviteToken>(0xAA), filled<session::RendezvousId>(0x11)};
    const json body = json::parse(encode_register_request(registration));
    EXPECT_EQ(body, (json{{"host_public_key", kSequential32},
                          {"invite_token", kAa32},
                          {"rendezvous_id", kEleven16}}));
}

TEST(InviteApiCodecTest, ResolveAndRevokeRequestsUseTheNormalizedCode) {
    const auto code = code_of("abcde-12345");
    EXPECT_EQ(json::parse(encode_resolve_request(code)), (json{{"code", "ABCDE12345"}}));
    EXPECT_EQ(json::parse(encode_revoke_request(code, filled<session::RevokeKey>(0xAA))),
              (json{{"code", "ABCDE12345"}, {"revoke_key", kAa32}}));
}

TEST(InviteApiCodecTest, AcceptsACreatedRegistration) {
    const HttpResponse response{201, json{{"code", "ABCDE12345"},
                                          {"display_code", "ABCDE-12345"},
                                          {"expires_at", "2026-11-08T12:30:00Z"},
                                          {"revoke_key", kAa32}}
                                         .dump()};
    const auto result = decode_register_response(response);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value().code, code_of("ABCDE12345"));
    EXPECT_EQ(result.value().revoke_key, filled<session::RevokeKey>(0xAA));
}

TEST(InviteApiCodecTest, RejectsARegistrationWithAShortRevokeKey) {
    const HttpResponse response{201,
                                json{{"code", "ABCDE12345"}, {"revoke_key", kEleven16}}.dump()};
    const auto result = decode_register_response(response);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), ErrorCode::kUnreachable);
}

TEST(InviteApiCodecTest, RejectsARegistrationWhoseCodeIsNotNormalized) {
    const HttpResponse response{201, json{{"code", "ABCDE-12345"}, {"revoke_key", kAa32}}.dump()};
    EXPECT_FALSE(decode_register_response(response).has_value());
}

TEST(InviteApiCodecTest, MapsRegistrationFailures) {
    EXPECT_EQ(decode_register_response({503, error_body("CAPACITY_EXCEEDED")}).error(),
              ErrorCode::kUnreachable);
    EXPECT_EQ(decode_register_response({500, error_body("INTERNAL")}).error(),
              ErrorCode::kUnreachable);
    EXPECT_EQ(decode_register_response({400, error_body("INVALID_REQUEST")}).error(),
              ErrorCode::kEngineError);
    // 途中のプロキシが返した HTML などは、状態コードだけで判断する。
    EXPECT_EQ(decode_register_response({502, "<html>Bad Gateway</html>"}).error(),
              ErrorCode::kUnreachable);
}

TEST(InviteApiCodecTest, AcceptsAResolvedInvite) {
    const auto code = code_of("ABCDE12345");
    const auto result = decode_resolve_response(
        code, {200, resolve_body("ABCDE12345", kSequential32, kAa32, kEleven16)});
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value().endpoint.host_public_key, sequential_key());
    EXPECT_EQ(result.value().endpoint.rendezvous_id, filled<session::RendezvousId>(0x11));
    EXPECT_EQ(result.value().invite_token, filled<session::InviteToken>(0xAA));
}

TEST(InviteApiCodecTest, RejectsAResolutionForAnotherCode) {
    const auto code = code_of("ABCDE12345");
    const auto result = decode_resolve_response(
        code, {200, resolve_body("ZZZZZ99999", kSequential32, kAa32, kEleven16)});
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), ErrorCode::kUnreachable);
}

TEST(InviteApiCodecTest, RejectsAResolutionWithAnInviteTokenOfAnotherLength) {
    const auto code = code_of("ABCDE12345");
    EXPECT_FALSE(decode_resolve_response(
                     code, {200, resolve_body("ABCDE12345", kSequential32, kEleven16, kEleven16)})
                     .has_value());
}

TEST(InviteApiCodecTest, RejectsAResolutionThatIsNotAJsonObject) {
    const auto code = code_of("ABCDE12345");
    EXPECT_FALSE(decode_resolve_response(code, {200, "[]"}).has_value());
    EXPECT_FALSE(decode_resolve_response(code, {200, "{"}).has_value());
    EXPECT_FALSE(decode_resolve_response(code, {200, ""}).has_value());
}

TEST(InviteApiCodecTest, MapsResolutionFailuresToTheClientErrorCodes) {
    const auto code = code_of("ABCDE12345");
    EXPECT_EQ(decode_resolve_response(code, {404, error_body("INVITE_NOT_FOUND")}).error(),
              ErrorCode::kInviteInvalid);
    EXPECT_EQ(decode_resolve_response(code, {400, error_body("INVALID_CODE")}).error(),
              ErrorCode::kInviteInvalid);
    EXPECT_EQ(decode_resolve_response(code, {410, error_body("INVITE_EXPIRED")}).error(),
              ErrorCode::kInviteExpired);
    EXPECT_EQ(decode_resolve_response(code, {410, error_body("INVITE_REVOKED")}).error(),
              ErrorCode::kInviteRevoked);
    EXPECT_EQ(decode_resolve_response(code, {500, error_body("INTERNAL")}).error(),
              ErrorCode::kUnreachable);
}

}  // namespace
}  // namespace vocroz::network::wire
