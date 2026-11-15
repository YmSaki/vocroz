// SPDX-License-Identifier: AGPL-3.0-only
//
// 状態とエラーコードの列挙子と、JSON での名前（仕様書の正規名）の対応表。
// 名前の対応はこの表だけに書き、エンコードとデコードの両方がこれを引く（知識を 1
// か所にまとめるため）。
#pragma once

#include <array>
#include <cstddef>
#include <optional>
#include <string_view>

#include "vocroz/control_protocol/states.h"

namespace vocroz::control_protocol::wire {

/// 列挙子と名前の組。
template <typename E>
struct EnumName {
    E value;
    std::string_view name;
};

/// 列挙型ごとの対応表。列挙子の宣言順に並べる。
template <typename E>
struct EnumNames;

template <>
struct EnumNames<SessionState> {
    static constexpr std::array<EnumName<SessionState>, 6> kTable{{
        {SessionState::kIdle, "IDLE"},
        {SessionState::kCreating, "CREATING"},
        {SessionState::kReady, "READY"},
        {SessionState::kActive, "ACTIVE"},
        {SessionState::kEnding, "ENDING"},
        {SessionState::kError, "ERROR"},
    }};
};

template <>
struct EnumNames<ConnectionState> {
    static constexpr std::array<EnumName<ConnectionState>, 6> kTable{{
        {ConnectionState::kPending, "PENDING"},
        {ConnectionState::kConnecting, "CONNECTING"},
        {ConnectionState::kConnected, "CONNECTED"},
        {ConnectionState::kReconnecting, "RECONNECTING"},
        {ConnectionState::kLost, "LOST"},
        {ConnectionState::kFailed, "FAILED"},
    }};
};

template <>
struct EnumNames<SignalState> {
    static constexpr std::array<EnumName<SignalState>, 4> kTable{{
        {SignalState::kReceiving, "RECEIVING"},
        {SignalState::kPaused, "PAUSED"},
        {SignalState::kResuming, "RESUMING"},
        {SignalState::kNoSignal, "NO_SIGNAL"},
    }};
};

template <>
struct EnumNames<OutputDeviceState> {
    static constexpr std::array<EnumName<OutputDeviceState>, 3> kTable{{
        {OutputDeviceState::kNone, "NONE"},
        {OutputDeviceState::kOpen, "OPEN"},
        {OutputDeviceState::kLost, "LOST"},
    }};
};

template <>
struct EnumNames<MasterMuteReason> {
    static constexpr std::array<EnumName<MasterMuteReason>, 2> kTable{{
        {MasterMuteReason::kOperator, "OPERATOR"},
        {MasterMuteReason::kSafety, "SAFETY"},
    }};
};

template <>
struct EnumNames<ClientState> {
    static constexpr std::array<EnumName<ClientState>, 8> kTable{{
        {ClientState::kDisconnected, "DISCONNECTED"},
        {ClientState::kResolving, "RESOLVING"},
        {ClientState::kAuthenticating, "AUTHENTICATING"},
        {ClientState::kWaitingApproval, "WAITING_APPROVAL"},
        {ClientState::kConnecting, "CONNECTING"},
        {ClientState::kConnected, "CONNECTED"},
        {ClientState::kReconnecting, "RECONNECTING"},
        {ClientState::kFailed, "FAILED"},
    }};
};

template <>
struct EnumNames<ErrorCode> {
    static constexpr std::array<EnumName<ErrorCode>, 12> kTable{{
        {ErrorCode::kRejected, "REJECTED"},
        {ErrorCode::kInviteExpired, "INVITE_EXPIRED"},
        {ErrorCode::kInviteRevoked, "INVITE_REVOKED"},
        {ErrorCode::kInviteInvalid, "INVITE_INVALID"},
        {ErrorCode::kUnreachable, "UNREACHABLE"},
        {ErrorCode::kReconnectTimeout, "RECONNECT_TIMEOUT"},
        {ErrorCode::kSessionEnded, "SESSION_ENDED"},
        {ErrorCode::kRevisionConflict, "REVISION_CONFLICT"},
        {ErrorCode::kNotFound, "NOT_FOUND"},
        {ErrorCode::kInvalidState, "INVALID_STATE"},
        {ErrorCode::kEngineError, "ENGINE_ERROR"},
        {ErrorCode::kDeviceUnavailable, "DEVICE_UNAVAILABLE"},
    }};
};

template <>
struct EnumNames<NoticeSeverity> {
    // 重要度は状態ではないので、UI の慣習に合わせて小文字にする。
    static constexpr std::array<EnumName<NoticeSeverity>, 2> kTable{{
        {NoticeSeverity::kInfo, "info"},
        {NoticeSeverity::kWarning, "warning"},
    }};
};

template <>
struct EnumNames<BusKind> {
    static constexpr std::array<EnumName<BusKind>, 2> kTable{{
        {BusKind::kUser, "USER"},
        {BusKind::kCue, "CUE"},
    }};
};

template <>
struct EnumNames<SendTap> {
    static constexpr std::array<EnumName<SendTap>, 2> kTable{{
        {SendTap::kPostFader, "POST_FADER"},
        {SendTap::kPreFader, "PRE_FADER"},
    }};
};

template <>
struct EnumNames<RoutingIssue> {
    static constexpr std::array<EnumName<RoutingIssue>, 12> kTable{{
        {RoutingIssue::kInvalidId, "INVALID_ID"},
        {RoutingIssue::kDuplicateId, "DUPLICATE_ID"},
        {RoutingIssue::kTooManyItems, "TOO_MANY_ITEMS"},
        {RoutingIssue::kUnknownNode, "UNKNOWN_NODE"},
        {RoutingIssue::kInvalidSend, "INVALID_SEND"},
        {RoutingIssue::kInvalidSendTap, "INVALID_SEND_TAP"},
        {RoutingIssue::kDelayOutOfRange, "DELAY_OUT_OF_RANGE"},
        {RoutingIssue::kOutputChannelOutOfRange, "OUTPUT_CHANNEL_OUT_OF_RANGE"},
        {RoutingIssue::kOverlappingOutputs, "OVERLAPPING_OUTPUTS"},
        {RoutingIssue::kUndelayedLoop, "UNDELAYED_LOOP"},
        {RoutingIssue::kUnknownPlugin, "UNKNOWN_PLUGIN"},
        {RoutingIssue::kUnsupportedPlugin, "UNSUPPORTED_PLUGIN"},
    }};
};

template <>
struct EnumNames<InputSourceKind> {
    static constexpr std::array<EnumName<InputSourceKind>, 2> kTable{{
        {InputSourceKind::kMicrophone, "MICROPHONE"},
        {InputSourceKind::kApplicationAudio, "APP_AUDIO"},
    }};
};

template <>
struct EnumNames<ExternalOutputState> {
    static constexpr std::array<EnumName<ExternalOutputState>, 3> kTable{{
        {ExternalOutputState::kStopped, "STOPPED"},
        {ExternalOutputState::kRunning, "RUNNING"},
        {ExternalOutputState::kTesting, "TESTING"},
    }};
};

template <>
struct EnumNames<ExternalOutputProfile> {
    static constexpr std::array<EnumName<ExternalOutputProfile>, 2> kTable{{
        {ExternalOutputProfile::kOpusStereo, "OPUS_STEREO"},
        {ExternalOutputProfile::kOpusMono, "OPUS_MONO"},
    }};
};

template <>
struct EnumNames<SrtpSuite> {
    static constexpr std::array<EnumName<SrtpSuite>, 2> kTable{{
        {SrtpSuite::kAesCm128HmacSha1_80, "AES_CM_128_HMAC_SHA1_80"},
        {SrtpSuite::kAesCm128HmacSha1_32, "AES_CM_128_HMAC_SHA1_32"},
    }};
};

template <>
struct EnumNames<ExternalOutputResult> {
    static constexpr std::array<EnumName<ExternalOutputResult>, 3> kTable{{
        {ExternalOutputResult::kOk, "OK"},
        {ExternalOutputResult::kResolveFailed, "RESOLVE_FAILED"},
        {ExternalOutputResult::kSendFailed, "SEND_FAILED"},
    }};
};

template <>
struct EnumNames<BlockedPluginReason> {
    static constexpr std::array<EnumName<BlockedPluginReason>, 3> kTable{{
        {BlockedPluginReason::kCrashed, "CRASHED"},
        {BlockedPluginReason::kTimedOut, "TIMED_OUT"},
        {BlockedPluginReason::kInvalidOutput, "INVALID_OUTPUT"},
    }};
};

template <>
struct EnumNames<PluginInsertState> {
    static constexpr std::array<EnumName<PluginInsertState>, 3> kTable{{
        {PluginInsertState::kLoading, "LOADING"},
        {PluginInsertState::kReady, "READY"},
        {PluginInsertState::kFailed, "FAILED"},
    }};
};

template <>
struct EnumNames<PluginInsertFailure> {
    static constexpr std::array<EnumName<PluginInsertFailure>, 4> kTable{{
        {PluginInsertFailure::kNotFound, "NOT_FOUND"},
        {PluginInsertFailure::kInstantiateFailed, "INSTANTIATE_FAILED"},
        {PluginInsertFailure::kUnsupportedLayout, "UNSUPPORTED_LAYOUT"},
        {PluginInsertFailure::kProcessingFailed, "PROCESSING_FAILED"},
    }};
};

/// 対応表が列挙子の宣言順（0, 1, 2, ...）に並んでいるかを確かめる。
/// 列挙子を追加したのに表を更新し忘れると、途中の抜けはここで、末尾の抜けは往復のテストで見つかる。
template <typename E>
constexpr bool is_in_declaration_order() {
    const auto& table = EnumNames<E>::kTable;
    for (std::size_t i = 0; i < table.size(); ++i) {
        if (static_cast<std::size_t>(table[i].value) != i) {
            return false;
        }
    }
    return true;
}

static_assert(is_in_declaration_order<SessionState>());
static_assert(is_in_declaration_order<ConnectionState>());
static_assert(is_in_declaration_order<SignalState>());
static_assert(is_in_declaration_order<OutputDeviceState>());
static_assert(is_in_declaration_order<MasterMuteReason>());
static_assert(is_in_declaration_order<ClientState>());
static_assert(is_in_declaration_order<ErrorCode>());
static_assert(is_in_declaration_order<NoticeSeverity>());
static_assert(is_in_declaration_order<BusKind>());
static_assert(is_in_declaration_order<SendTap>());
static_assert(is_in_declaration_order<RoutingIssue>());
static_assert(is_in_declaration_order<InputSourceKind>());
static_assert(is_in_declaration_order<ExternalOutputState>());
static_assert(is_in_declaration_order<ExternalOutputProfile>());
static_assert(is_in_declaration_order<SrtpSuite>());
static_assert(is_in_declaration_order<ExternalOutputResult>());
static_assert(is_in_declaration_order<BlockedPluginReason>());
static_assert(is_in_declaration_order<PluginInsertState>());
static_assert(is_in_declaration_order<PluginInsertFailure>());

/// 列挙子の JSON での名前を返す。
template <typename E>
[[nodiscard]] constexpr std::string_view name_of(E value) noexcept {
    for (const auto& entry : EnumNames<E>::kTable) {
        if (entry.value == value) {
            return entry.name;
        }
    }
    // 表は宣言順で抜けがないことを static_assert で確かめているので、ここには来ない。
    return {};
}

/// JSON での名前から列挙子を返す。大文字と小文字は区別する。
/// @return 該当する列挙子。未知の名前なら std::nullopt
template <typename E>
[[nodiscard]] constexpr std::optional<E> value_of(std::string_view name) noexcept {
    for (const auto& entry : EnumNames<E>::kTable) {
        if (entry.name == name) {
            return entry.value;
        }
    }
    return std::nullopt;
}

}  // namespace vocroz::control_protocol::wire
