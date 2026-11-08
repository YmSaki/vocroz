// SPDX-License-Identifier: AGPL-3.0-only

// Package signaling は UDP ホールパンチングのための候補アドレス交換を扱うドメインのパッケージ。
//
// 親機と子機はそれぞれ同じランデブー ID でサーバーに登録する。
// 両者がそろうと、サーバーは双方に相手の候補アドレス（サーバーから見た外部アドレスと、
// 本人が申告したローカルアドレス）を同時に通知する。通知を受けた双方が同時に相手へ送信することで、
// NAT のマッピングが開く。通知を同時に送ることが「ホールパンチ開始の同期」になる。
//
// 登録はソフトステートで、一定時間更新がなければ消える。永続化はしない。
// サーバーが止まっても、確立済みの P2P セッションには影響しない。
package signaling

import (
	"errors"
	"net/netip"
	"slices"
	"sync"
	"time"
)

const (
	// IDLen はランデブー ID とピア ID のバイト数（128 bit）。
	IDLen = 16

	// MaxLocalCandidates は 1 回の登録で申告できるローカル候補アドレスの上限。
	// 一般的な PC のインターフェース数（有線、無線、IPv4 と IPv6）に余裕を持たせた値で、
	// 通知パケットを 1 つの UDP データグラムに収めるための上限でもある。
	MaxLocalCandidates = 8

	// RegistrationTTL は登録を保持する時間。クライアントはこれより短い間隔で登録し直す。
	// 親機は NAT のマッピングを保つためにも 10 秒程度の間隔で登録し直す想定。
	RegistrationTTL = 30 * time.Second

	// MaxClientsPerSession は 1 つのランデブー ID に同時に登録できる子機の数。
	// 子機の同時接続の目標（8 台以上）と、再接続中の重複登録に余裕を持たせた値。
	MaxClientsPerSession = 32

	// MaxSessions は同時に保持するランデブー ID の数。メモリを使い切られないための上限。
	MaxSessions = 10_000
)

// ErrCapacity は登録数の上限に達したことを表す。
var ErrCapacity = errors.New("signaling capacity exceeded")

// ID はランデブー ID かピア ID。
type ID [IDLen]byte

// Role は登録者の役割。
type Role uint8

const (
	// RoleHost は親機（pa-engine）。
	RoleHost Role = 1
	// RoleClient は子機。
	RoleClient Role = 2
)

// Registration は 1 回の登録の内容。
type Registration struct {
	// RendezvousID は親機と子機が落ち合う ID。招待情報で子機に渡る。
	RendezvousID ID
	// PeerID は登録者を区別する ID。親機はセッションごと、子機は参加の試行ごとに乱数で決める。
	PeerID ID
	Role   Role
	// Observed はサーバーから見た送信元アドレス（サーバー再帰アドレス）。
	Observed netip.AddrPort
	// Local は登録者が申告したローカルアドレス。同じ LAN にいる場合の直結に使う。MaxLocalCandidates 個まで。
	Local []netip.AddrPort
}

// Introduction は「To に、相手のピアと候補アドレスを知らせる」通知。
type Introduction struct {
	To         netip.AddrPort
	PeerID     ID
	PeerRole   Role
	Candidates []netip.AddrPort
}

// Result は登録の結果。
type Result struct {
	// PeerPresent は相手の役割（子機なら親機、親機なら子機）の登録がすでにあるかどうか。
	PeerPresent bool
	// Introductions は送信する通知。相手がいれば、登録者と相手の双方への通知が含まれる。
	Introductions []Introduction
}

// entry は保持している登録。
type entry struct {
	peerID     ID
	candidates []netip.AddrPort
	observed   netip.AddrPort
	expiresAt  time.Time
}

// session は 1 つのランデブー ID に属する登録。
type session struct {
	host    *entry
	clients map[ID]*entry
}

// Table は登録を保持し、親機と子機を引き合わせる。すべてのメソッドは複数の goroutine から同時に呼び出してよい。
type Table struct {
	now func() time.Time

	mu       sync.Mutex
	sessions map[ID]*session
}

// NewTable は空の Table を返す。now は現在時刻（テストで差し替えるために引数で受け取る）。
func NewTable(now func() time.Time) *Table {
	return &Table{now: now, sessions: make(map[ID]*session)}
}

// Register は登録を追加か更新し、送るべき通知を返す。
//
// 子機の登録では、親機がいればその子機と親機の双方への通知を返す。
// 親機の登録では、登録済みのすべての子機について双方への通知を返す。
// 子機は接続できるまで登録を繰り返すので、そのたびに通知が出てホールパンチが再試行される。
// 上限に達していれば ErrCapacity を返す。
func (t *Table) Register(reg Registration) (Result, error) {
	now := t.now()
	e := &entry{
		peerID:     reg.PeerID,
		candidates: candidatesOf(reg),
		observed:   reg.Observed,
		expiresAt:  now.Add(RegistrationTTL),
	}

	t.mu.Lock()
	defer t.mu.Unlock()
	s, ok := t.sessions[reg.RendezvousID]
	if !ok {
		if len(t.sessions) >= MaxSessions {
			return Result{}, ErrCapacity
		}
		s = &session{clients: make(map[ID]*entry)}
		t.sessions[reg.RendezvousID] = s
	}
	s.pruneLocked(now)

	var res Result
	switch reg.Role {
	case RoleHost:
		// 親機が回線を切り替えた場合も、最新の登録で置き換える。
		s.host = e
		for _, c := range s.clients {
			res.Introductions = append(res.Introductions, introducePair(e, c)...)
		}
		res.PeerPresent = len(s.clients) > 0
	case RoleClient:
		if _, exists := s.clients[reg.PeerID]; !exists && len(s.clients) >= MaxClientsPerSession {
			return Result{}, ErrCapacity
		}
		s.clients[reg.PeerID] = e
		if s.host != nil {
			res.Introductions = introducePair(s.host, e)
			res.PeerPresent = true
		}
	}
	return res, nil
}

// Sweep は期限切れの登録と、空になったランデブー ID を消す。定期的に呼び出す。
func (t *Table) Sweep() {
	t.mu.Lock()
	defer t.mu.Unlock()
	now := t.now()
	for id, s := range t.sessions {
		s.pruneLocked(now)
		if s.host == nil && len(s.clients) == 0 {
			delete(t.sessions, id)
		}
	}
}

// pruneLocked は期限切れの登録を消す。Table.mu を保持して呼び出す。
func (s *session) pruneLocked(now time.Time) {
	if s.host != nil && !now.Before(s.host.expiresAt) {
		s.host = nil
	}
	for id, c := range s.clients {
		if !now.Before(c.expiresAt) {
			delete(s.clients, id)
		}
	}
}

// introducePair は親機と子機の双方に相手を知らせる通知を作る。
func introducePair(host, client *entry) []Introduction {
	return []Introduction{
		{To: host.observed, PeerID: client.peerID, PeerRole: RoleClient, Candidates: client.candidates},
		{To: client.observed, PeerID: host.peerID, PeerRole: RoleHost, Candidates: host.candidates},
	}
}

// candidatesOf は登録者の候補アドレスを、サーバー再帰アドレスを先頭にして重複なく並べる。
// NAT 越しの相手には再帰アドレスが最も有望なので先頭に置く。
func candidatesOf(reg Registration) []netip.AddrPort {
	out := make([]netip.AddrPort, 0, 1+len(reg.Local))
	out = append(out, reg.Observed)
	for _, c := range reg.Local {
		if !slices.Contains(out, c) {
			out = append(out, c)
		}
	}
	return out
}
