// SPDX-License-Identifier: AGPL-3.0-only
//
// 子機と親機の間の制御メッセージのエンコードとデコード。

#include "wire/peer_message_codec.h"

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>

#include "detail/text_utils.h"
#include "session_constants.h"

namespace vocroz::session::wire {
namespace {

using control_protocol::ErrorCode;

// 種別の値。子機から親機は 0x01 から、親機から子機は 0x81 から振り、向きを取り違えても
// 同じ値にならないようにする。値は互換のために変えない（変えるなら新しい値を足す）。
enum class Type : std::uint8_t {
    kJoinRequest = 0x01,
    kReconnectRequest = 0x02,
    kSendMuteReport = 0x03,
    kInputReport = 0x04,
    kLeaveNotice = 0x05,
    kJoinPending = 0x81,
    kJoinApproved = 0x82,
    kJoinDenied = 0x83,
    kSessionEnded = 0x84,
};

// JoinDenied の理由の値。ErrorCode の宣言順に依存させず、ワイヤー上の値を固定する。
enum class DenyReason : std::uint8_t {
    kRejected = 1,
    kInviteExpired = 2,
    kInviteRevoked = 3,
    kInviteInvalid = 4,
    kReconnectTimeout = 5,
    kSessionEnded = 6,
};

// 真偽値のバイトは 0 か 1 だけを受け付ける。それ以外は改変か不具合なので不正とする。
constexpr std::uint8_t kFalse = 0;
constexpr std::uint8_t kTrue = 1;

class Writer {
public:
    explicit Writer(Type type) {
        out_.push_back(static_cast<std::uint8_t>(type));
    }

    void u8(std::uint8_t value) {
        out_.push_back(value);
    }

    void boolean(bool value) {
        out_.push_back(value ? kTrue : kFalse);
    }

    void u32(std::uint32_t value) {
        for (int shift = 24; shift >= 0; shift -= 8) {
            out_.push_back(static_cast<std::uint8_t>(value >> shift));
        }
    }

    template <std::size_t N>
    void bytes(const std::array<std::uint8_t, N>& value) {
        out_.insert(out_.end(), value.begin(), value.end());
    }

    // 文字列は呼び出し側で上限（255 バイト以下）を検証済みであること。
    void text(std::string_view value) {
        out_.push_back(static_cast<std::uint8_t>(value.size()));
        out_.insert(out_.end(), value.begin(), value.end());
    }

    std::vector<std::uint8_t> finish() && {
        return std::move(out_);
    }

private:
    std::vector<std::uint8_t> out_;
};

// 先頭の種別を読んだ後の本文を読む。読み過ぎたら ok() が false になる。
class Reader {
public:
    explicit Reader(std::span<const std::uint8_t> bytes) : bytes_(bytes) {}

    std::uint8_t u8() {
        if (!need(1)) {
            return 0;
        }
        return bytes_[pos_++];
    }

    bool boolean() {
        const std::uint8_t value = u8();
        if (value != kFalse && value != kTrue) {
            ok_ = false;
        }
        return value == kTrue;
    }

    std::uint32_t u32() {
        if (!need(4)) {
            return 0;
        }
        std::uint32_t value = 0;
        for (int i = 0; i < 4; ++i) {
            value = (value << 8) | bytes_[pos_++];
        }
        return value;
    }

    template <std::size_t N>
    std::array<std::uint8_t, N> bytes() {
        std::array<std::uint8_t, N> out{};
        if (!need(N)) {
            return out;
        }
        for (std::size_t i = 0; i < N; ++i) {
            out[i] = bytes_[pos_++];
        }
        return out;
    }

    // 長さ付きの文字列を読み、表示してよい UTF-8 かを検証する。
    std::string text(std::size_t max_bytes) {
        const std::size_t length = u8();
        if (!need(length)) {
            return {};
        }
        std::string out(reinterpret_cast<const char*>(bytes_.data() + pos_), length);
        pos_ += length;
        if (!detail::is_displayable_text(out, max_bytes)) {
            ok_ = false;
        }
        return out;
    }

    // すべて読み切り、途中で不正がなければ true。
    [[nodiscard]] bool finished() const noexcept {
        return ok_ && pos_ == bytes_.size();
    }

private:
    bool need(std::size_t n) noexcept {
        if (!ok_ || bytes_.size() - pos_ < n) {
            ok_ = false;
            return false;
        }
        return true;
    }

    std::span<const std::uint8_t> bytes_;
    std::size_t pos_ = 0;
    bool ok_ = true;
};

DenyReason to_wire(ErrorCode reason) noexcept {
    switch (reason) {
        case ErrorCode::kInviteExpired:
            return DenyReason::kInviteExpired;
        case ErrorCode::kInviteRevoked:
            return DenyReason::kInviteRevoked;
        case ErrorCode::kInviteInvalid:
            return DenyReason::kInviteInvalid;
        case ErrorCode::kReconnectTimeout:
            return DenyReason::kReconnectTimeout;
        case ErrorCode::kSessionEnded:
            return DenyReason::kSessionEnded;
        // 拒否の理由に当たらないコードは、最も安全な「承認されなかった」として送る。
        case ErrorCode::kRejected:
        case ErrorCode::kUnreachable:
        case ErrorCode::kRevisionConflict:
        case ErrorCode::kNotFound:
        case ErrorCode::kInvalidState:
        case ErrorCode::kEngineError:
        case ErrorCode::kDeviceUnavailable:
            return DenyReason::kRejected;
    }
    return DenyReason::kRejected;
}

std::optional<ErrorCode> from_wire(std::uint8_t value) noexcept {
    switch (static_cast<DenyReason>(value)) {
        case DenyReason::kRejected:
            return ErrorCode::kRejected;
        case DenyReason::kInviteExpired:
            return ErrorCode::kInviteExpired;
        case DenyReason::kInviteRevoked:
            return ErrorCode::kInviteRevoked;
        case DenyReason::kInviteInvalid:
            return ErrorCode::kInviteInvalid;
        case DenyReason::kReconnectTimeout:
            return ErrorCode::kReconnectTimeout;
        case DenyReason::kSessionEnded:
            return ErrorCode::kSessionEnded;
    }
    return std::nullopt;
}

template <typename T>
common::Result<T, PeerMessageError> finish(const Reader& reader, T message) {
    if (!reader.finished()) {
        return common::make_unexpected(PeerMessageError::kMalformed);
    }
    return message;
}

}  // namespace

std::vector<std::uint8_t> encode(const ClientToHostMessage& message) {
    return std::visit(
        [](const auto& m) {
            using M = std::decay_t<decltype(m)>;
            if constexpr (std::is_same_v<M, JoinRequest>) {
                Writer w{Type::kJoinRequest};
                w.bytes(m.invite_token);
                w.boolean(m.reconnect_token.has_value());
                if (m.reconnect_token) {
                    w.bytes(*m.reconnect_token);
                }
                w.text(m.display_name);
                return std::move(w).finish();
            } else if constexpr (std::is_same_v<M, ReconnectRequest>) {
                Writer w{Type::kReconnectRequest};
                w.bytes(m.reconnect_token);
                return std::move(w).finish();
            } else if constexpr (std::is_same_v<M, SendMuteReport>) {
                Writer w{Type::kSendMuteReport};
                w.boolean(m.send_muted);
                w.u32(m.epoch);
                return std::move(w).finish();
            } else if constexpr (std::is_same_v<M, InputReport>) {
                Writer w{Type::kInputReport};
                w.boolean(m.input_missing);
                return std::move(w).finish();
            } else {
                static_assert(std::is_same_v<M, LeaveNotice>);
                return Writer{Type::kLeaveNotice}.finish();
            }
        },
        message);
}

std::vector<std::uint8_t> encode(const HostToClientMessage& message) {
    return std::visit(
        [](const auto& m) {
            using M = std::decay_t<decltype(m)>;
            if constexpr (std::is_same_v<M, JoinPending>) {
                Writer w{Type::kJoinPending};
                w.text(m.session_name);
                return std::move(w).finish();
            } else if constexpr (std::is_same_v<M, JoinApproved>) {
                Writer w{Type::kJoinApproved};
                w.bytes(m.reconnect_token);
                return std::move(w).finish();
            } else if constexpr (std::is_same_v<M, JoinDenied>) {
                Writer w{Type::kJoinDenied};
                w.u8(static_cast<std::uint8_t>(to_wire(m.reason)));
                return std::move(w).finish();
            } else {
                static_assert(std::is_same_v<M, SessionEndedNotice>);
                return Writer{Type::kSessionEnded}.finish();
            }
        },
        message);
}

common::Result<ClientToHostMessage, PeerMessageError> decode_client_to_host(
    std::span<const std::uint8_t> bytes) {
    if (bytes.empty()) {
        return common::make_unexpected(PeerMessageError::kMalformed);
    }
    Reader r{bytes.subspan(1)};
    switch (static_cast<Type>(bytes[0])) {
        case Type::kJoinRequest: {
            JoinRequest m;
            m.invite_token = r.bytes<kInviteTokenBytes>();
            if (r.boolean()) {
                m.reconnect_token = r.bytes<std::tuple_size_v<ReconnectToken>>();
            }
            m.display_name = r.text(kMaxDisplayNameBytes);
            return finish<ClientToHostMessage>(r, std::move(m));
        }
        case Type::kReconnectRequest:
            return finish<ClientToHostMessage>(
                r, ReconnectRequest{r.bytes<std::tuple_size_v<ReconnectToken>>()});
        case Type::kSendMuteReport: {
            SendMuteReport m;
            m.send_muted = r.boolean();
            m.epoch = r.u32();
            return finish<ClientToHostMessage>(r, m);
        }
        case Type::kInputReport:
            return finish<ClientToHostMessage>(r, InputReport{r.boolean()});
        case Type::kLeaveNotice:
            return finish<ClientToHostMessage>(r, LeaveNotice{});
        case Type::kJoinPending:
        case Type::kJoinApproved:
        case Type::kJoinDenied:
        case Type::kSessionEnded:
            break;
    }
    return common::make_unexpected(PeerMessageError::kUnknownType);
}

common::Result<HostToClientMessage, PeerMessageError> decode_host_to_client(
    std::span<const std::uint8_t> bytes) {
    if (bytes.empty()) {
        return common::make_unexpected(PeerMessageError::kMalformed);
    }
    Reader r{bytes.subspan(1)};
    switch (static_cast<Type>(bytes[0])) {
        case Type::kJoinPending:
            return finish<HostToClientMessage>(r, JoinPending{r.text(kMaxSessionNameBytes)});
        case Type::kJoinApproved:
            return finish<HostToClientMessage>(
                r, JoinApproved{r.bytes<std::tuple_size_v<ReconnectToken>>()});
        case Type::kJoinDenied: {
            const auto reason = from_wire(r.u8());
            if (!reason) {
                return common::make_unexpected(PeerMessageError::kMalformed);
            }
            return finish<HostToClientMessage>(r, JoinDenied{*reason});
        }
        case Type::kSessionEnded:
            return finish<HostToClientMessage>(r, SessionEndedNotice{});
        case Type::kJoinRequest:
        case Type::kReconnectRequest:
        case Type::kSendMuteReport:
        case Type::kInputReport:
        case Type::kLeaveNotice:
            break;
    }
    return common::make_unexpected(PeerMessageError::kUnknownType);
}

}  // namespace vocroz::session::wire
