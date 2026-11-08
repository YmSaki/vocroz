// SPDX-License-Identifier: AGPL-3.0-only
//
// composition_root.h の実装。

#include "composition_root.h"

#include <iostream>
#include <utility>

#include "vocroz/update/manifest_verifier.h"
#include "vocroz/update/update_applier.h"
#include "vocroz/update/update_checker.h"
#include "vocroz/version.h"

#include "update_config.h"
#include "updater_constants.h"
#include "win/win32_process.h"
#include "win/win32_text.h"

namespace vocroz::updater {
namespace {

namespace fs = std::filesystem;

[[nodiscard]] std::string utf8_of(const fs::path& path) {
    return win::narrow(path.wstring());
}

[[nodiscard]] update::ManifestVerifier make_verifier() {
    return update::ManifestVerifier{update::parse_public_key_hex(kPublicKeyHex)};
}

[[nodiscard]] update::SemanticVersion running_version() {
    // kVersion は CMake の project() のバージョンで、パースできない値にはならない。
    return update::parse_semantic_version(kVersion).value_or(update::SemanticVersion{});
}

}  // namespace

CompositionRoot::CompositionRoot(LaunchOptions options, fs::path install_dir, fs::path data_dir)
    : options_(std::move(options)),
      install_dir_(std::move(install_dir)),
      data_dir_(std::move(data_dir)),
      install_layout_(install_dir_),
      package_(data_dir_ / kUpdatesDirName, install_dir_),
      settings_backup_(data_dir_),
      code_signature_verifier_(install_dir_),
      health_checker_(install_dir_),
      session_monitor_(utf8_of(data_dir_)),
      update_source_(std::string{kManifestUrl}, data_dir_ / kUpdatesDirName) {}

int CompositionRoot::run() {
    switch (options_.command) {
        case Command::kCheck:
            return run_check();
        case Command::kApply: {
            const int code = run_apply();
            restart_if_requested();
            return code;
        }
        case Command::kVersion:
            break;
    }
    return kExitUsage;
}

int CompositionRoot::run_check() {
    if (kManifestUrl.empty()) {
        // 配信先を埋め込んでいないビルド（開発用）は、更新を確認しない。
        return kExitOk;
    }
    update::UpdateChecker checker{make_verifier(), running_version(), update_source_, package_};
    switch (checker.check()) {
        case update::CheckOutcome::kUpToDate:
            return kExitOk;
        case update::CheckOutcome::kUpdateStaged:
            return kExitUpdateStaged;
        case update::CheckOutcome::kRejected:
            std::cerr << "update rejected: signature or hash mismatch\n";
            return kExitRejected;
        case update::CheckOutcome::kUnavailable:
            break;
    }
    std::cerr << "update check failed: network unavailable\n";
    return kExitFatal;
}

int CompositionRoot::run_apply() {
    const auto manifest = package_.read_staged_manifest();
    if (!manifest) {
        return kExitOk;
    }
    update::UpdateApplier applier{
        make_verifier(), running_version(),
        update::UpdateApplierPorts{session_monitor_, package_, settings_backup_, install_layout_,
                                   code_signature_verifier_, health_checker_}};
    // --apply を受け取るのは、利用者が「更新して再起動」を押したときだけ（remote-pa が渡す）。
    switch (applier.apply(*manifest, /*user_requested=*/true)) {
        case update::ApplyOutcome::kApplied:
            package_.discard();
            return kExitOk;
        case update::ApplyOutcome::kBlockedBySession:
            return kExitBlockedBySession;
        case update::ApplyOutcome::kNotNewer:
            // すでに適用済みか古い更新。残しておく意味がないので消す。
            package_.discard();
            return kExitOk;
        case update::ApplyOutcome::kRejected:
            return kExitRejected;
        case update::ApplyOutcome::kRolledBack:
            std::cerr << "update rolled back: the new version failed the startup check\n";
            return kExitRolledBack;
        case update::ApplyOutcome::kNotRequested:
        case update::ApplyOutcome::kAborted:
            break;
    }
    return kExitFatal;
}

void CompositionRoot::restart_if_requested() {
    if (!options_.restart) {
        return;
    }
    // 適用に失敗した場合も再起動する。remote-pa は「更新して再起動」で終了しているので、
    // 起動しないと利用者の画面が消えたままになる。どの版を起動するかは起動スタブが決める。
    std::vector<std::wstring> args;
    for (const auto& arg : options_.restart_args) {
        args.push_back(win::widen(arg));
    }
    if (!win::start_detached(install_dir_ / kLauncherFileName, args)) {
        std::cerr << "failed to restart VOCROZ\n";
    }
}

}  // namespace vocroz::updater
