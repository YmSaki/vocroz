// SPDX-License-Identifier: AGPL-3.0-only
//
// update_checker.h の実装。

#include "vocroz/update/update_checker.h"

#include <utility>
#include <variant>

#include "package_verification.h"

namespace vocroz::update {

UpdateChecker::UpdateChecker(ManifestVerifier verifier, SemanticVersion running_version,
                             ports::UpdateSource& source, ports::StagedPackage& package)
    : verifier_(std::move(verifier)),
      running_version_(std::move(running_version)),
      source_(source),
      package_(package) {}

CheckOutcome UpdateChecker::check() {
    const auto text = source_.fetch_manifest();
    if (!text) {
        return CheckOutcome::kUnavailable;
    }
    const ManifestResult verified = verifier_.verify(*text);
    if (std::holds_alternative<ManifestError>(verified)) {
        return CheckOutcome::kRejected;
    }
    const auto& manifest = std::get<UpdateManifest>(verified);
    // 古い版や同じ版のマニフェストは、配信側の更新漏れか巻き戻しの攻撃なので、ダウンロードしない。
    if (!(running_version_ < manifest.version)) {
        return CheckOutcome::kUpToDate;
    }
    if (!source_.download_package(manifest)) {
        package_.discard();
        return CheckOutcome::kUnavailable;
    }
    if (!package_matches_manifest(package_, manifest)) {
        package_.discard();
        return CheckOutcome::kRejected;
    }
    if (!source_.store_manifest(*text)) {
        package_.discard();
        return CheckOutcome::kUnavailable;
    }
    return CheckOutcome::kUpdateStaged;
}

}  // namespace vocroz::update
