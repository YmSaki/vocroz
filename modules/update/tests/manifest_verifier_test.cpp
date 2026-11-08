// SPDX-License-Identifier: AGPL-3.0-only
//
// ManifestVerifier（署名と形式の検証）の単体テスト。

#include "vocroz/update/manifest_verifier.h"

#include <string>
#include <variant>

#include <gtest/gtest.h>

#include "manifest_test_support.h"

namespace vocroz::update {
namespace {

using testing::make_key_pair;
using testing::manifest_body;
using testing::sign_manifest;

const std::string kSha(64, 'a');

ManifestError error_of(const ManifestResult& result) {
    EXPECT_TRUE(std::holds_alternative<ManifestError>(result));
    return std::holds_alternative<ManifestError>(result) ? std::get<ManifestError>(result)
                                                         : ManifestError::kMalformed;
}

class ManifestVerifierTest : public ::testing::Test {
protected:
    testing::TestKeyPair key_ = make_key_pair(0x11);
    ManifestVerifier verifier_{key_.public_key};
};

TEST_F(ManifestVerifierTest, AcceptsSignedManifestAndExposesFields) {
    const auto result =
        verifier_.verify(sign_manifest(manifest_body("0.1.0-alpha.2", kSha, 1234, "VOCROZ"), key_));
    ASSERT_TRUE(std::holds_alternative<UpdateManifest>(result));
    const auto& manifest = std::get<UpdateManifest>(result);
    EXPECT_EQ(to_string(manifest.version), "0.1.0-alpha.2");
    EXPECT_EQ(manifest.package_url, "https://example.invalid/pkg.zip");
    EXPECT_EQ(manifest.package_size, 1234U);
    EXPECT_EQ(manifest.package_sha256[0], 0xAA);
    EXPECT_EQ(manifest.publisher, "VOCROZ");
}

TEST_F(ManifestVerifierTest, AcceptsManifestSignedByReleaseTool) {
    // tools/release（Go の crypto/ed25519）がシード 0x00〜0x1f の鍵で署名した値。
    // C++ と Go で署名対象のバイト列の解釈が一致していることを確かめる。
    const auto public_key =
        parse_public_key_hex("03a107bff3ce10be1d70dd18e74bc09967e4d6309ba50d5f1ddc8664125531b8");
    ASSERT_TRUE(public_key.has_value());
    const std::string text =
        "format=1\nproduct=vocroz\nversion=0.1.0-alpha.2\n"
        "url=https://example.invalid/vocroz-0.1.0-alpha.2.zip\n"
        "sha256=abababababababababababababababababababababababababababababababab\n"
        "size=1234\npublisher=\n"
        "signature=49d614e5f674d3fe7789a9489d590b743f9c6db729892b19e95d0e6a7f96e282"
        "23fe90a3e722ce5908b7eb2de6d4b7ac6be131f2651b74cebc2d98416b41bb09\n";
    EXPECT_TRUE(std::holds_alternative<UpdateManifest>(ManifestVerifier{public_key}.verify(text)));
}

TEST_F(ManifestVerifierTest, RejectsEverythingWithoutEmbeddedKey) {
    const ManifestVerifier no_key{std::nullopt};
    EXPECT_EQ(error_of(no_key.verify(sign_manifest(manifest_body("1.0.0", kSha, 1), key_))),
              ManifestError::kNoPublicKey);
}

TEST_F(ManifestVerifierTest, RejectsTamperedBody) {
    std::string text = sign_manifest(manifest_body("1.0.0", kSha, 1234), key_);
    text.replace(text.find("size=1234"), 9, "size=1235");
    EXPECT_EQ(error_of(verifier_.verify(text)), ManifestError::kBadSignature);
}

TEST_F(ManifestVerifierTest, RejectsManifestSignedByAnotherKey) {
    const auto other = make_key_pair(0x22);
    EXPECT_EQ(error_of(verifier_.verify(sign_manifest(manifest_body("1.0.0", kSha, 1), other))),
              ManifestError::kBadSignature);
}

TEST_F(ManifestVerifierTest, RejectsTrailingDataAfterSignature) {
    const std::string text = sign_manifest(manifest_body("1.0.0", kSha, 1), key_) + "extra\n";
    EXPECT_EQ(error_of(verifier_.verify(text)), ManifestError::kMalformed);
}

TEST_F(ManifestVerifierTest, RejectsMissingOrMalformedSignatureLine) {
    EXPECT_EQ(error_of(verifier_.verify(manifest_body("1.0.0", kSha, 1))),
              ManifestError::kMalformed);
    EXPECT_EQ(error_of(verifier_.verify(manifest_body("1.0.0", kSha, 1) + "signature=zz\n")),
              ManifestError::kMalformed);
    EXPECT_EQ(error_of(verifier_.verify("")), ManifestError::kMalformed);
}

TEST_F(ManifestVerifierTest, RejectsReorderedKeysEvenWhenSigned) {
    const std::string body =
        "product=vocroz\nformat=1\nversion=1.0.0\nurl=https://e.invalid/p\n"
        "sha256=" +
        kSha + "\nsize=1\npublisher=\n";
    EXPECT_EQ(error_of(verifier_.verify(sign_manifest(body, key_))), ManifestError::kMalformed);
}

TEST_F(ManifestVerifierTest, RejectsCarriageReturnInBody) {
    std::string body = manifest_body("1.0.0", kSha, 1);
    body.insert(body.find('\n'), "\r");
    EXPECT_EQ(error_of(verifier_.verify(sign_manifest(body, key_))), ManifestError::kMalformed);
}

TEST_F(ManifestVerifierTest, RejectsUnsupportedFormatAndOtherProduct) {
    std::string body = manifest_body("1.0.0", kSha, 1);
    body.replace(0, 8, "format=2");
    EXPECT_EQ(error_of(verifier_.verify(sign_manifest(body, key_))),
              ManifestError::kUnsupportedFormat);

    body = manifest_body("1.0.0", kSha, 1);
    body.replace(body.find("vocroz"), 6, "other!");
    EXPECT_EQ(error_of(verifier_.verify(sign_manifest(body, key_))), ManifestError::kWrongProduct);
}

TEST_F(ManifestVerifierTest, RejectsPlainHttpUrl) {
    const auto body = manifest_body("1.0.0", kSha, 1, "", "http://example.invalid/pkg.zip");
    EXPECT_EQ(error_of(verifier_.verify(sign_manifest(body, key_))), ManifestError::kInsecureUrl);
}

TEST_F(ManifestVerifierTest, RejectsInvalidFields) {
    EXPECT_EQ(error_of(verifier_.verify(sign_manifest(manifest_body("1.0", kSha, 1), key_))),
              ManifestError::kInvalidField);
    EXPECT_EQ(error_of(verifier_.verify(
                  sign_manifest(manifest_body("1.0.0", std::string(64, 'A'), 1), key_))),
              ManifestError::kInvalidField);
    std::string body = manifest_body("1.0.0", kSha, 1);
    body.replace(body.find("size=1"), 6, "size=01");
    EXPECT_EQ(error_of(verifier_.verify(sign_manifest(body, key_))), ManifestError::kInvalidField);
}

TEST_F(ManifestVerifierTest, RejectsEmptyOrOversizedPackage) {
    EXPECT_EQ(error_of(verifier_.verify(sign_manifest(manifest_body("1.0.0", kSha, 0), key_))),
              ManifestError::kTooLarge);
    const std::uint64_t too_large = 512ULL * 1024ULL * 1024ULL + 1ULL;
    EXPECT_EQ(
        error_of(verifier_.verify(sign_manifest(manifest_body("1.0.0", kSha, too_large), key_))),
        ManifestError::kTooLarge);
}

TEST(ParsePublicKeyHexTest, AcceptsOnly64LowercaseHexDigits) {
    EXPECT_TRUE(parse_public_key_hex(std::string(64, 'f')).has_value());
    EXPECT_FALSE(parse_public_key_hex("").has_value());
    EXPECT_FALSE(parse_public_key_hex(std::string(63, 'f')).has_value());
    EXPECT_FALSE(parse_public_key_hex(std::string(64, 'F')).has_value());
}

}  // namespace
}  // namespace vocroz::update
