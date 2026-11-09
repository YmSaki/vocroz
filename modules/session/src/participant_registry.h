// SPDX-License-Identifier: AGPL-3.0-only
//
// 親機の参加者（承認待ちとチャンネルストリップ）の一覧。
// ストリップは承認時に作って participant_id に固定し、再接続しても作り直さない。
// 並び順は承認した順で、本番中に並べ替えない（ストリップは末尾に足すだけ）。
// 1 台の子機は複数の入力（ストリーム）を送れ、入力ごとに 1 本のストリップになる
// （docs/adr/0035-client-input-expansion.md）。同じ子機のストリップは participant_id、接続、
// 再接続トークン、接続状態を共有し、一覧には 1 本ずつのエントリとして並ぶ。後から足した入力の
// ストリップも末尾に足すので、ストリップの並びは UI の表示と常に同じになる。
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "vocroz/common/ports/clock.h"
#include "vocroz/control_protocol/entities.h"
#include "vocroz/session/identifiers.h"
#include "vocroz/session/input_stream.h"

namespace vocroz::session {

/// 承認待ちの参加者。
struct PendingJoin {
    control_protocol::PendingParticipant entry;
    /// 申請が届いた接続。
    PeerHandle peer = 0;
    /// 子機が申告した入力。申告が届くまではストリーム 0 のモノラルのマイク 1 本とみなす。
    std::vector<InputStream> streams{InputStream{}};
};

/// ストリップを持つ参加者の、ストリップ 1 本（子機の入力 1 つ）ぶんの状態。
struct Participant {
    /// UI に送るストリップの状態。正となる値はここにだけ持つ。
    control_protocol::ChannelStrip strip;
    /// このストリップに流すメディアのストリーム ID。
    std::uint16_t stream_id = 0;
    /// 承認時に発行した再接続トークン。セッションの終了かストリップの削除まで有効。
    ReconnectToken reconnect_token{};
    /// 今つながっている接続。LOST と FAILED では持たない。
    std::optional<PeerHandle> peer;
    /// 子機から通知された入力デバイスの欠落。
    bool input_missing = false;
    /// RECONNECTING に入った時刻。再接続の猶予（120 秒）の起点。
    std::optional<common::Clock::TimePoint> reconnecting_since;
    /// 最後に適用したフェーダーのシーケンス番号。
    std::optional<control_protocol::Sequence> last_gain_seq;
};

/// 参加者の一覧。
/// スレッド安全性: コントロールスレッドからだけ使う。
class ParticipantRegistry {
public:
    /// 承認待ちの参加者を足す。entity_id と短い識別 ID を振る。
    /// @param peer 申請が届いた接続
    /// @param name 検証済みの表示名
    /// @return 足した参加者
    PendingJoin& add_pending(PeerHandle peer, std::string name);

    /// entity_id の承認待ちの参加者を返す。なければ nullptr。
    [[nodiscard]] PendingJoin* find_pending(std::string_view entity_id) noexcept;

    /// 接続 peer から申請した承認待ちの参加者を返す。なければ nullptr。
    [[nodiscard]] PendingJoin* find_pending_by_peer(PeerHandle peer) noexcept;

    /// 承認待ちの参加者を取り除く。
    void remove_pending(std::string_view entity_id);

    /// 承認待ちの一覧（申請の順）を返す。
    [[nodiscard]] std::vector<control_protocol::PendingParticipant> pending_entries() const;

    /// 承認待ちの人数を返す。
    [[nodiscard]] std::size_t pending_count() const noexcept {
        return pending_.size();
    }

    /// 承認待ちの参加者に、申告した入力ごとのストリップを作り、末尾に足す。
    /// 承認待ちの一覧からは取り除かない。
    /// ストリップは CONNECTING、チャンネルミュート、送信ミュート、0 dB で始まる（初期ミュート）。
    /// @param pending 承認する参加者
    /// @param token   発行した再接続トークン
    /// @return 足した 1 本目のストリップ。以降の追加で無効になるので、続けて保持しない
    Participant& add_participant(const PendingJoin& pending, const ReconnectToken& token);

    /// 承認済みの参加者に、入力 1 つぶんのストリップを足す（入力の申告が変わったとき）。
    /// 名前、接続、再接続トークン、接続状態、送信ミュートは同じ参加者のストリップから写す。
    /// 新しいストリップはチャンネルミュート、0 dB で始まる（初期ミュート）。
    /// @param participant_id 参加者。ストリップが 1 本以上あること
    /// @param stream         入力
    /// @return 足したストリップ。以降の追加で無効になるので、続けて保持しない
    Participant& add_stream(std::string_view participant_id, const InputStream& stream);

    /// 参加者のストリップを、並び順で返す。
    /// @return ストリップへのポインター。一覧への追加と削除で無効になる
    [[nodiscard]] std::vector<Participant*> strips_of(std::string_view participant_id);

    /// 参加者のストリップの入力の番号（同じ種類の中での 1 からの番号）を、ストリーム ID の順に
    /// 振り直す。
    /// @return 番号の変わったストリップ
    std::vector<Participant*> renumber(std::string_view participant_id);

    /// entity_id のストリップの参加者を返す。なければ nullptr。
    [[nodiscard]] Participant* find(std::string_view entity_id) noexcept;

    /// 接続 peer につながっている参加者を返す。なければ nullptr。
    [[nodiscard]] Participant* find_by_peer(PeerHandle peer) noexcept;

    /// 再接続トークンが token の参加者を返す。なければ nullptr。
    /// トークンは定数時間で比べる。
    [[nodiscard]] Participant* find_by_token(const ReconnectToken& token) noexcept;

    /// ストリップを 1 本取り除く。同じ参加者のほかのストリップは残る。
    void remove(std::string_view entity_id);

    /// 参加者のストリップをすべて取り除く。再接続トークンも無効になる。
    void remove_participant(std::string_view participant_id);

    /// ストリップの一覧（並び順）を返す。
    [[nodiscard]] std::vector<Participant>& participants() noexcept {
        return participants_;
    }

    /// ストリップの一覧（並び順）を返す。
    [[nodiscard]] const std::vector<Participant>& participants() const noexcept {
        return participants_;
    }

    /// CONNECTED か RECONNECTING の参加者がいれば true を返す（セッションの ACTIVE の条件）。
    [[nodiscard]] bool any_active() const noexcept;

    /// すべての参加者を取り除く（セッションの終了）。番号の振り直しは次のセッションから。
    void clear() noexcept;

private:
    std::vector<PendingJoin> pending_;
    std::vector<Participant> participants_;
    // 番号はセッションの中で使い回さない。削除したストリップの entity_id を UI が覚えていても、
    // 別の参加者のストリップと取り違えないようにするため。
    std::uint64_t next_pending_number_ = 1;
    std::uint64_t next_strip_number_ = 1;
    std::uint64_t next_short_id_ = 1;
};

}  // namespace vocroz::session
