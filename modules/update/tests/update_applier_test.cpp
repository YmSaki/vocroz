// SPDX-License-Identifier: AGPL-3.0-only
//
// UpdateApplier（更新の適用、本番中のブロック、ロールバック）の単体テスト。

#include "vocroz/update/update_applier.h"

#include <cstdint>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "vocroz/update/install_pointer.h"

#include "fakes/fake_code_signature_verifier.h"
#include "fakes/fake_health_checker.h"
#include "fakes/fake_install_layout.h"
#include "fakes/fake_session_monitor.h"
#include "fakes/fake_settings_backup.h"
#include "fakes/fake_staged_package.h"
#include "manifest_test_support.h"

namespace vocroz::update {
namespace {

const SemanticVersion kRunning{0, 1, 0, "alpha.1"};
const SemanticVersion kNext{0, 1, 0, "alpha.2"};

class UpdateApplierTest : public ::testing::Test {
protected:
    UpdateApplierTest() {
        package_.bytes = std::vector<std::uint8_t>(100, 0x5A);
        layout_.versions = {kRunning};
    }

    std::string manifest(std::string_view publisher = "") {
        return testing::sign_manifest(
            testing::manifest_body(to_string(kNext), testing::sha256_hex(package_.bytes),
                                   package_.bytes.size(), publisher),
            key_);
    }

    ApplyOutcome apply(const std::string& text, bool user_requested = true) {
        UpdateApplier applier{
            ManifestVerifier{key_.public_key}, kRunning,
            UpdateApplierPorts{session_, package_, backup_, layout_, signature_, health_}};
        return applier.apply(text, user_requested);
    }

    [[nodiscard]] std::optional<InstallPointer> pointer() const {
        return layout_.pointer ? parse_install_pointer(*layout_.pointer) : std::nullopt;
    }

    testing::TestKeyPair key_ = testing::make_key_pair(0x33);
    fakes::FakeSessionMonitor session_;
    fakes::FakeInstallLayout layout_;
    fakes::FakeStagedPackage package_{layout_};
    fakes::FakeSettingsBackup backup_;
    fakes::FakeCodeSignatureVerifier signature_;
    fakes::FakeHealthChecker health_;
};

TEST_F(UpdateApplierTest, AppliesVerifiedUpdateAndSwitchesPointer) {
    EXPECT_EQ(apply(manifest()), ApplyOutcome::kApplied);
    EXPECT_EQ(pointer(), (InstallPointer{kNext, kRunning}));
    EXPECT_TRUE(layout_.has(kNext));
    EXPECT_TRUE(layout_.has(kRunning));
    EXPECT_EQ(backup_.backup_calls, 1);
    EXPECT_EQ(backup_.restore_calls, 0);
}

TEST_F(UpdateApplierTest, BlocksDuringSessionWithoutTouchingAnything) {
    session_.active = true;
    EXPECT_EQ(apply(manifest()), ApplyOutcome::kBlockedBySession);
    EXPECT_EQ(package_.extract_calls, 0);
    EXPECT_EQ(backup_.backup_calls, 0);
    EXPECT_FALSE(layout_.pointer.has_value());
    // 検証済みのパッケージはセッション終了後の適用のために残す。
    EXPECT_FALSE(package_.discarded);
}

TEST_F(UpdateApplierTest, DoesNothingWithoutUserRequest) {
    EXPECT_EQ(apply(manifest(), false), ApplyOutcome::kNotRequested);
    EXPECT_EQ(package_.extract_calls, 0);
}

TEST_F(UpdateApplierTest, RejectsBadManifestAndDiscardsPackage) {
    std::string text = manifest();
    text[text.find("alpha.2")] = 'b';
    EXPECT_EQ(apply(text), ApplyOutcome::kRejected);
    EXPECT_TRUE(package_.discarded);
    EXPECT_EQ(package_.extract_calls, 0);
}

TEST_F(UpdateApplierTest, RejectsPackageWhoseHashDoesNotMatch) {
    const std::string text = manifest();
    package_.bytes[10] ^= 0x01;
    EXPECT_EQ(apply(text), ApplyOutcome::kRejected);
    EXPECT_TRUE(package_.discarded);
    EXPECT_EQ(package_.extract_calls, 0);
    EXPECT_FALSE(layout_.pointer.has_value());
}

TEST_F(UpdateApplierTest, RejectsPackageLargerThanManifest) {
    const std::string text = manifest();
    package_.bytes.push_back(0);
    EXPECT_EQ(apply(text), ApplyOutcome::kRejected);
}

TEST_F(UpdateApplierTest, AbortsWithoutChangesWhenBackupFails) {
    backup_.fail_backup = true;
    EXPECT_EQ(apply(manifest()), ApplyOutcome::kAborted);
    EXPECT_EQ(package_.extract_calls, 0);
    EXPECT_FALSE(layout_.pointer.has_value());
}

TEST_F(UpdateApplierTest, RollsBackWhenHealthCheckFails) {
    health_.healthy = false;
    EXPECT_EQ(apply(manifest()), ApplyOutcome::kRolledBack);
    EXPECT_FALSE(layout_.has(kNext));
    EXPECT_TRUE(layout_.has(kRunning));
    EXPECT_FALSE(layout_.pointer.has_value());
    EXPECT_EQ(backup_.restore_calls, 1);
}

TEST_F(UpdateApplierTest, RollsBackWhenExtractionFails) {
    package_.fail_extract = true;
    EXPECT_EQ(apply(manifest()), ApplyOutcome::kRolledBack);
    EXPECT_EQ(health_.calls, 0);
    EXPECT_FALSE(layout_.pointer.has_value());
}

TEST_F(UpdateApplierTest, RollsBackWhenPointerCannotBeWritten) {
    layout_.fail_write_pointer = true;
    EXPECT_EQ(apply(manifest()), ApplyOutcome::kRolledBack);
    EXPECT_FALSE(layout_.has(kNext));
}

TEST_F(UpdateApplierTest, SkipsAuthenticodeWhilePublisherIsEmpty) {
    EXPECT_EQ(apply(manifest("")), ApplyOutcome::kApplied);
    EXPECT_EQ(signature_.calls, 0);
}

TEST_F(UpdateApplierTest, VerifiesAuthenticodeWhenPublisherIsSet) {
    EXPECT_EQ(apply(manifest("CN=VOCROZ")), ApplyOutcome::kApplied);
    EXPECT_EQ(signature_.calls, 1);
    EXPECT_EQ(signature_.last_publisher, "CN=VOCROZ");
}

TEST_F(UpdateApplierTest, RollsBackWhenAuthenticodeFails) {
    signature_.trusted = false;
    EXPECT_EQ(apply(manifest("CN=VOCROZ")), ApplyOutcome::kRolledBack);
    EXPECT_EQ(health_.calls, 0);
    EXPECT_FALSE(layout_.has(kNext));
}

TEST_F(UpdateApplierTest, KeepsOnlyPreviousVersionAfterSwitch) {
    const SemanticVersion older{0, 0, 9, ""};
    layout_.versions = {older, kRunning};
    layout_.pointer = serialize_install_pointer({kRunning, older});
    EXPECT_EQ(apply(manifest()), ApplyOutcome::kApplied);
    EXPECT_EQ(pointer(), (InstallPointer{kNext, kRunning}));
    EXPECT_FALSE(layout_.has(older));
    EXPECT_TRUE(layout_.has(kRunning));
}

TEST_F(UpdateApplierTest, RefusesToReapplySameVersion) {
    UpdateApplier applier{
        ManifestVerifier{key_.public_key}, kNext,
        UpdateApplierPorts{session_, package_, backup_, layout_, signature_, health_}};
    EXPECT_EQ(applier.apply(manifest(), true), ApplyOutcome::kNotNewer);
}

// 残っていた旧版の pa-updater（kRunning）を直接実行しても、ポインターの現行版と同じ候補は
// 新しい版として扱わず、展開にも進まない。
TEST_F(UpdateApplierTest, StaleUpdaterRefusesCandidateEqualToInstalledCurrent) {
    layout_.versions = {kRunning, kNext};
    layout_.pointer = serialize_install_pointer({kNext, kRunning});
    package_.fail_extract = true;
    EXPECT_EQ(apply(manifest()), ApplyOutcome::kNotNewer);
    EXPECT_EQ(package_.extract_calls, 0);
    EXPECT_EQ(backup_.backup_calls, 0);
    EXPECT_TRUE(layout_.has(kNext));
    EXPECT_EQ(pointer(), (InstallPointer{kNext, kRunning}));
}

// 旧版の pa-updater から、ポインターの現行版より古い候補へのダウングレードを受け付けない。
TEST_F(UpdateApplierTest, StaleUpdaterRefusesDowngradeBelowInstalledCurrent) {
    const SemanticVersion installed{0, 1, 0, "alpha.3"};
    layout_.versions = {kRunning, installed};
    layout_.pointer = serialize_install_pointer({installed, kRunning});
    EXPECT_EQ(apply(manifest()), ApplyOutcome::kNotNewer);
    EXPECT_EQ(package_.extract_calls, 0);
    EXPECT_TRUE(layout_.has(installed));
    EXPECT_EQ(pointer(), (InstallPointer{installed, kRunning}));
}

// ポインターが壊れているときは、これまでどおり動いている pa-updater の版と比べる。
TEST_F(UpdateApplierTest, FallsBackToRunningVersionWhenPointerIsBroken) {
    layout_.pointer = "broken";
    EXPECT_EQ(apply(manifest()), ApplyOutcome::kApplied);
    EXPECT_EQ(pointer(), (InstallPointer{kNext, kRunning}));
}

// 起動確認に失敗しても、ポインターが指す現行版と直前の版のフォルダは消さない。
// 候補の版のフォルダが（以前の失敗などで）すでにあっても、ポインターが指していなければ消す。
TEST_F(UpdateApplierTest, RollbackNeverRemovesVersionsThePointerReferences) {
    const SemanticVersion older{0, 0, 9, ""};
    layout_.versions = {older, kRunning};
    layout_.pointer = serialize_install_pointer({kRunning, older});
    health_.healthy = false;
    EXPECT_EQ(apply(manifest()), ApplyOutcome::kRolledBack);
    EXPECT_FALSE(layout_.has(kNext));
    EXPECT_TRUE(layout_.has(kRunning));
    EXPECT_TRUE(layout_.has(older));
    EXPECT_EQ(pointer(), (InstallPointer{kRunning, older}));
}

}  // namespace
}  // namespace vocroz::update
