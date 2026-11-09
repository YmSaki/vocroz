// SPDX-License-Identifier: AGPL-3.0-only
//
// メッセージの C++ の型と、JSON の type に書く種別の名前の対応。
// 種別の名前はここにだけ書き、エンコードとデコードの両方がこれを引く。
#pragma once

#include <cstddef>
#include <optional>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

#include "vocroz/control_protocol/commands.h"
#include "vocroz/control_protocol/events.h"

namespace vocroz::control_protocol::wire {

/// メッセージの型ごとの種別の名前。メッセージでない型は空で、使うと static_assert で止まる。
template <typename T>
inline constexpr std::string_view kMessageType{};

// UI からエンジンへ。
template <>
inline constexpr std::string_view kMessageType<Hello> = "hello";
template <>
inline constexpr std::string_view kMessageType<SetGain> = "set_gain";
template <>
inline constexpr std::string_view kMessageType<CreateSession> = "create_session";
template <>
inline constexpr std::string_view kMessageType<EndSession> = "end_session";
template <>
inline constexpr std::string_view kMessageType<SetChannelMute> = "set_channel_mute";
template <>
inline constexpr std::string_view kMessageType<SetMasterMute> = "set_master_mute";
template <>
inline constexpr std::string_view kMessageType<ApproveParticipant> = "approve_participant";
template <>
inline constexpr std::string_view kMessageType<RejectParticipant> = "reject_participant";
template <>
inline constexpr std::string_view kMessageType<RemoveStrip> = "remove_strip";
template <>
inline constexpr std::string_view kMessageType<Join> = "join";
template <>
inline constexpr std::string_view kMessageType<Leave> = "leave";
template <>
inline constexpr std::string_view kMessageType<SelectInput> = "select_input";
template <>
inline constexpr std::string_view kMessageType<SetSendMute> = "set_send_mute";
template <>
inline constexpr std::string_view kMessageType<StageRouting> = "stage_routing";
template <>
inline constexpr std::string_view kMessageType<CommitRouting> = "commit_routing";
template <>
inline constexpr std::string_view kMessageType<DiscardRouting> = "discard_routing";
template <>
inline constexpr std::string_view kMessageType<SetRouteMute> = "set_route_mute";
template <>
inline constexpr std::string_view kMessageType<SetReturnMute> = "set_return_mute";

// エンジンから UI へ。
template <>
inline constexpr std::string_view kMessageType<MixerSnapshotEvent> = "mixer_snapshot";
template <>
inline constexpr std::string_view kMessageType<ClientSnapshotEvent> = "client_snapshot";
template <>
inline constexpr std::string_view kMessageType<SessionUpdated> = "session_updated";
template <>
inline constexpr std::string_view kMessageType<ChannelUpdated> = "channel_updated";
template <>
inline constexpr std::string_view kMessageType<ChannelRemoved> = "channel_removed";
template <>
inline constexpr std::string_view kMessageType<MasterUpdated> = "master_updated";
template <>
inline constexpr std::string_view kMessageType<PendingUpdated> = "pending_updated";
template <>
inline constexpr std::string_view kMessageType<ClientUpdated> = "client_updated";
template <>
inline constexpr std::string_view kMessageType<RoutingUpdated> = "routing_updated";
template <>
inline constexpr std::string_view kMessageType<RouteLevelUpdated> = "route_level_updated";
template <>
inline constexpr std::string_view kMessageType<Ack> = "ack";
template <>
inline constexpr std::string_view kMessageType<Reject> = "reject";
template <>
inline constexpr std::string_view kMessageType<GainAck> = "gain_ack";
template <>
inline constexpr std::string_view kMessageType<Meters> = "meters";
template <>
inline constexpr std::string_view kMessageType<Notice> = "notice";

/// 値の型の種別の名前を返す。
template <typename T>
[[nodiscard]] constexpr std::string_view type_name_of(const T& /*value*/) noexcept {
    static_assert(!kMessageType<T>.empty(), "kMessageType の特殊化がない");
    return kMessageType<T>;
}

namespace dispatch {

template <typename Variant, typename Read, std::size_t... I>
std::optional<Variant> read_alternative(std::string_view type, Read& read,
                                        std::index_sequence<I...> /*indices*/) {
    static_assert((!kMessageType<std::variant_alternative_t<I, Variant>>.empty() && ...),
                  "kMessageType の特殊化がない");
    std::optional<Variant> result;
    // 種別の名前が一致した型だけを read で読む。一致したら以降の型は試さない。
    (void)((type == kMessageType<std::variant_alternative_t<I, Variant>>
                ? (result.emplace(
                       read(std::type_identity<std::variant_alternative_t<I, Variant>>{})),
                   true)
                : false) ||
           ...);
    return result;
}

}  // namespace dispatch

/// Variant の型のうち、種別の名前が type と一致するものを read で読む。
/// @param read `T(std::type_identity<T>)` の関数。Variant の各型 T について呼び出せること
/// @return 読んだ値。どの型の名前とも一致しなければ std::nullopt
template <typename Variant, typename Read>
[[nodiscard]] std::optional<Variant> read_alternative(std::string_view type, Read read) {
    return dispatch::read_alternative<Variant>(
        type, read, std::make_index_sequence<std::variant_size_v<Variant>>{});
}

}  // namespace vocroz::control_protocol::wire
