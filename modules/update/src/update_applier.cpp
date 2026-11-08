// SPDX-License-Identifier: AGPL-3.0-only
//
// update_applier.h の実装。状態遷移は state/update_state_machine の表に従い、
// ここは各状態で行う処理（ポートの呼び出し）だけを持つ。

#include "vocroz/update/update_applier.h"

#include <utility>
#include <variant>

#include "vocroz/update/install_pointer.h"
#include "vocroz/update/update_policy.h"

#include "package_verification.h"
#include "state/update_state_machine.h"

namespace vocroz::update {
namespace {

using state::UpdateEvent;
using state::UpdateState;

[[nodiscard]] ApplyOutcome outcome_of(ApplyDecision decision) {
    switch (decision) {
        case ApplyDecision::kBlockedBySession:
            return ApplyOutcome::kBlockedBySession;
        case ApplyDecision::kNotRequested:
            return ApplyOutcome::kNotRequested;
        case ApplyDecision::kNotNewer:
            return ApplyOutcome::kNotNewer;
        case ApplyDecision::kApply:
            break;
    }
    // kApply はこの関数に渡さない。万一渡されたら、何も変えない側に倒す。
    return ApplyOutcome::kAborted;
}

}  // namespace

UpdateApplier::UpdateApplier(ManifestVerifier verifier, SemanticVersion running_version,
                             UpdateApplierPorts ports)
    : verifier_(std::move(verifier)), running_version_(std::move(running_version)), ports_(ports) {}

ApplyOutcome UpdateApplier::apply(std::string_view manifest_text, bool user_requested) {
    const ManifestResult verified = verifier_.verify(manifest_text);
    if (std::holds_alternative<ManifestError>(verified)) {
        ports_.package.discard();
        return ApplyOutcome::kRejected;
    }
    const auto& manifest = std::get<UpdateManifest>(verified);

    const ApplyDecision decision = decide_apply({
        .session_active = ports_.session_monitor.is_session_active(),
        .user_requested = user_requested,
        .current = running_version_,
        .candidate = manifest.version,
    });
    if (decision != ApplyDecision::kApply) {
        return outcome_of(decision);
    }

    UpdateState current = state::next_state(UpdateState::kIdle, UpdateEvent::kStart);
    const auto advance = [&current](bool succeeded) {
        current = state::next_state(
            current, succeeded ? UpdateEvent::kStepSucceeded : UpdateEvent::kStepFailed);
    };

    // 各段階は前の段階が成功したときだけ実行する。失敗したら遷移表が終端か
    // kRollingBack に進めるので、ループを抜けて後始末をする。
    while (!state::is_terminal(current) && current != UpdateState::kRollingBack) {
        switch (current) {
            case UpdateState::kVerifyingPackage:
                advance(verify_package(manifest));
                if (current == UpdateState::kAborted) {
                    ports_.package.discard();
                    return ApplyOutcome::kRejected;
                }
                break;
            case UpdateState::kBackingUp:
                advance(ports_.settings_backup.backup());
                break;
            case UpdateState::kExtracting:
                advance(ports_.package.extract(manifest.version));
                break;
            case UpdateState::kVerifyingSignature:
                advance(verify_signature(manifest));
                break;
            case UpdateState::kCheckingHealth:
                advance(ports_.health_checker.check(manifest.version));
                break;
            case UpdateState::kSwitching:
                advance(switch_to(manifest.version));
                break;
            case UpdateState::kIdle:
            case UpdateState::kCommitted:
            case UpdateState::kRollingBack:
            case UpdateState::kRolledBack:
            case UpdateState::kAborted:
                // ループの条件で除いているので来ない。
                break;
        }
    }

    switch (current) {
        case UpdateState::kCommitted:
            return ApplyOutcome::kApplied;
        case UpdateState::kRollingBack:
            roll_back(manifest.version);
            current = state::next_state(current, UpdateEvent::kRollbackFinished);
            return ApplyOutcome::kRolledBack;
        case UpdateState::kAborted:
            return ApplyOutcome::kAborted;
        case UpdateState::kIdle:
        case UpdateState::kVerifyingPackage:
        case UpdateState::kBackingUp:
        case UpdateState::kExtracting:
        case UpdateState::kVerifyingSignature:
        case UpdateState::kCheckingHealth:
        case UpdateState::kSwitching:
        case UpdateState::kRolledBack:
            break;
    }
    return ApplyOutcome::kAborted;
}

bool UpdateApplier::verify_package(const UpdateManifest& manifest) {
    return package_matches_manifest(ports_.package, manifest);
}

bool UpdateApplier::verify_signature(const UpdateManifest& manifest) {
    // 発行者が空のマニフェストは、署名証明書が届くまでの未署名ビルド用。
    // マニフェスト自体は Ed25519 で署名済みなので、発行者を空にできるのは鍵の持ち主だけ。
    if (manifest.publisher.empty()) {
        return true;
    }
    return ports_.code_signature_verifier.verify(manifest.version, manifest.publisher);
}

bool UpdateApplier::switch_to(const SemanticVersion& version) {
    // ポインターが壊れているかない場合（インストーラーが入れた初期状態）は、
    // 動いている版を直前の版として記録する。
    const auto existing_text = ports_.install_layout.read_pointer();
    const auto existing = existing_text ? parse_install_pointer(*existing_text) : std::nullopt;
    const SemanticVersion previous = existing ? existing->current : running_version_;
    if (!ports_.install_layout.write_pointer(
            serialize_install_pointer(InstallPointer{version, previous}))) {
        return false;
    }
    prune_old_versions(version, previous);
    return true;
}

void UpdateApplier::prune_old_versions(const SemanticVersion& current,
                                       const SemanticVersion& previous) {
    // 古い版は直前の 1 つだけ残す。削除に失敗しても、次の更新で再び試せばよいので無視する。
    for (const SemanticVersion& version : ports_.install_layout.installed_versions()) {
        if (version != current && version != previous) {
            ports_.install_layout.remove_version(version);
        }
    }
}

void UpdateApplier::roll_back(const SemanticVersion& version) {
    // ポインターは切り替えに成功したときだけ書き換わるので、ここで戻す必要はない。
    // 展開したフォルダを消し、設定を退避した内容に戻せば、旧版がそのまま起動できる。
    ports_.install_layout.remove_version(version);
    // 復元に失敗しても、旧版の設定ファイルは更新の手順で書き換えていないので、起動はできる。
    static_cast<void>(ports_.settings_backup.restore());
}

}  // namespace vocroz::update
