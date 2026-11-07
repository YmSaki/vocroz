// SPDX-License-Identifier: AGPL-3.0-only
//
// UpdateChecker（更新の確認とダウンロードの検証）の単体テスト。

#include "vocroz/update/update_checker.h"

#include <cstdint>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "fakes/fake_install_layout.h"
#include "fakes/fake_staged_package.h"
#include "fakes/fake_update_source.h"
#include "manifest_test_support.h"

namespace vocroz::update {
namespace {

const SemanticVersion kRunning{0, 1, 0, "alpha.1"};

class UpdateCheckerTest : public ::testing::Test {
protected:
    UpdateCheckerTest() {
        package_.bytes = std::vector<std::uint8_t>(64, 0x42);
    }

    std::string manifest(std::string_view version) {
        return testing::sign_manifest(
            testing::manifest_body(version, testing::sha256_hex(package_.bytes),
                                   package_.bytes.size()),
            key_);
    }

    CheckOutcome check() {
        UpdateChecker checker{ManifestVerifier{key_.public_key}, kRunning, source_, package_};
        return checker.check();
    }

    testing::TestKeyPair key_ = testing::make_key_pair(0x44);
    fakes::FakeUpdateSource source_;
    fakes::FakeInstallLayout layout_;
    fakes::FakeStagedPackage package_{layout_};
};

TEST_F(UpdateCheckerTest, StagesVerifiedNewerVersion) {
    source_.manifest = manifest("0.1.0-alpha.2");
    EXPECT_EQ(check(), CheckOutcome::kUpdateStaged);
    EXPECT_EQ(source_.stored_manifest, source_.manifest);
    EXPECT_FALSE(package_.discarded);
}

TEST_F(UpdateCheckerTest, ReportsUpToDateWithoutDownloading) {
    source_.manifest = manifest("0.1.0-alpha.1");
    EXPECT_EQ(check(), CheckOutcome::kUpToDate);
    source_.manifest = manifest("0.0.9");
    EXPECT_EQ(check(), CheckOutcome::kUpToDate);
    EXPECT_EQ(source_.download_calls, 0);
}

TEST_F(UpdateCheckerTest, ReportsUnavailableWhenOffline) {
    EXPECT_EQ(check(), CheckOutcome::kUnavailable);
}

TEST_F(UpdateCheckerTest, RejectsUnsignedManifest) {
    source_.manifest = testing::manifest_body("0.2.0", std::string(64, 'a'), 1);
    EXPECT_EQ(check(), CheckOutcome::kRejected);
    EXPECT_EQ(source_.download_calls, 0);
}

TEST_F(UpdateCheckerTest, DiscardsCorruptedDownload) {
    source_.manifest = manifest("0.2.0");
    package_.bytes[0] ^= 0xFF;
    EXPECT_EQ(check(), CheckOutcome::kRejected);
    EXPECT_TRUE(package_.discarded);
    EXPECT_FALSE(source_.stored_manifest.has_value());
}

TEST_F(UpdateCheckerTest, DiscardsInterruptedDownload) {
    source_.manifest = manifest("0.2.0");
    source_.fail_download = true;
    EXPECT_EQ(check(), CheckOutcome::kUnavailable);
    EXPECT_TRUE(package_.discarded);
}

}  // namespace
}  // namespace vocroz::update
