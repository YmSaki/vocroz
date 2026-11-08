// SPDX-License-Identifier: AGPL-3.0-only

// Package invite は招待の登録、解決、失効を扱うドメインのパッケージ。
//
// 親機は招待トークン、静的公開鍵、ランデブー ID を登録し、短い招待コードを受け取る。
// 子機は招待コードから招待情報を引き、親機へ参加を申請する。
// 招待コードは参加を申請する権利を与えるだけで、音声は親機が承認するまで流れない。
// サーバーはメディア鍵を持たず、招待トークンと公開鍵は親機から預かって渡すだけである。
//
// 保存先はメモリ（単一ノード）。再起動すると有効な招待は消えるが、
// 招待の寿命は 30 分なので、親機が招待を作り直せば済む（docs/adr/0012-rendezvous-protocol.md）。
package invite

import (
	"crypto/sha256"
	"crypto/subtle"
	"errors"
	"fmt"
	"io"
	"sync"
	"time"
)

const (
	// MaxTTL は招待の有効期限の上限。仕様の設計値（30 分）で、親機がこれより長く指定しても切り詰める。
	MaxTTL = 30 * time.Minute

	// MinTokenLen は招待トークンの最小バイト数。仕様が 128 bit 以上のエントロピーを求めているため。
	MinTokenLen = 16

	// MaxTokenLen は招待トークンの最大バイト数。メモリを際限なく使わせないための上限。
	MaxTokenLen = 64

	// PublicKeyLen は親機の静的公開鍵のバイト数（Noise_IK の X25519 鍵、docs/adr/0001-key-exchange-noise-ik.md）。
	PublicKeyLen = 32

	// RendezvousIDLen はランデブー ID のバイト数（128 bit。推測できない値にするため）。
	RendezvousIDLen = 16

	// RevokeKeyLen は失効用の鍵のバイト数（256 bit）。
	RevokeKeyLen = 32

	// MaxInvites は同時に保持する招待（失効の記録を含む）の上限。
	// 単一ノードのメモリを使い切られないようにするため。通常の運用ではこの値に達しない。
	MaxInvites = 100_000

	// tombstoneRetention は期限切れや失効の後も記録を残す時間。
	// この間は子機に「期限切れ」「失効済み」を区別して返せる。過ぎると「見つからない」になる。
	tombstoneRetention = 24 * time.Hour

	// maxCodeAttempts は招待コードが既存のものと衝突したときに作り直す回数の上限。
	maxCodeAttempts = 8
)

// 解決と失効のエラー。httpapi がエラーコードに変換する。
var (
	ErrNotFound      = errors.New("invite not found")
	ErrExpired       = errors.New("invite expired")
	ErrRevoked       = errors.New("invite revoked")
	ErrForbidden     = errors.New("revoke key mismatch")
	ErrInvalidInvite = errors.New("invalid invite registration")
	ErrCapacity      = errors.New("invite capacity exceeded")
)

// Registration は親機が登録する招待の内容。
type Registration struct {
	// HostPublicKey は親機の静的公開鍵。子機が Noise_IK で親機を認証するために使う。
	HostPublicKey [PublicKeyLen]byte
	// Token は招待トークン（MinTokenLen〜MaxTokenLen バイト）。親機が生成し、子機が参加の申請で親機に示す。
	Token []byte
	// RendezvousID は UDP のランデブーで親機と子機が落ち合うための ID。セッション単位で親機が決める。
	RendezvousID [RendezvousIDLen]byte
	// TTL は有効期限。0 なら MaxTTL、MaxTTL を超える値は MaxTTL に切り詰める。
	TTL time.Duration
}

// Invite は子機に渡す招待情報。
type Invite struct {
	Code          Code
	HostPublicKey [PublicKeyLen]byte
	Token         []byte
	RendezvousID  [RendezvousIDLen]byte
	ExpiresAt     time.Time
}

// Issued は招待の登録結果。RevokeKey は親機だけが持ち、手動の失効に使う。
type Issued struct {
	Invite    Invite
	RevokeKey [RevokeKeyLen]byte
}

// record は保存する招待。失効用の鍵はハッシュだけを持つ。
type record struct {
	invite        Invite
	revokeKeyHash [sha256.Size]byte
	revoked       bool
	// finishedAt は期限切れか失効になった時刻。記録を消す時刻の起点にする。
	finishedAt time.Time
}

// Registry は招待をメモリに保持する。すべてのメソッドは複数の goroutine から同時に呼び出してよい。
type Registry struct {
	now  func() time.Time
	rand io.Reader

	mu      sync.Mutex
	records map[Code]*record
}

// NewRegistry は空の Registry を返す。
// now は現在時刻、rand は招待コードと失効用の鍵の乱数源（本番では crypto/rand.Reader）。
// テストで時刻と乱数を差し替えられるように引数で受け取る。
func NewRegistry(now func() time.Time, rand io.Reader) *Registry {
	return &Registry{now: now, rand: rand, records: make(map[Code]*record)}
}

// Register は招待を登録し、招待コードと失効用の鍵を返す。
// 内容が不正なら ErrInvalidInvite、保持数の上限に達していれば ErrCapacity を返す。
func (r *Registry) Register(reg Registration) (Issued, error) {
	if len(reg.Token) < MinTokenLen || len(reg.Token) > MaxTokenLen || reg.TTL < 0 {
		return Issued{}, ErrInvalidInvite
	}
	ttl := reg.TTL
	if ttl == 0 || ttl > MaxTTL {
		ttl = MaxTTL
	}

	var revokeKey [RevokeKeyLen]byte
	if _, err := io.ReadFull(r.rand, revokeKey[:]); err != nil {
		return Issued{}, fmt.Errorf("generate revoke key: %w", err)
	}

	r.mu.Lock()
	defer r.mu.Unlock()
	if len(r.records) >= MaxInvites {
		return Issued{}, ErrCapacity
	}
	code, err := r.uniqueCodeLocked()
	if err != nil {
		return Issued{}, err
	}
	inv := Invite{
		Code:          code,
		HostPublicKey: reg.HostPublicKey,
		Token:         append([]byte(nil), reg.Token...),
		RendezvousID:  reg.RendezvousID,
		ExpiresAt:     r.now().Add(ttl),
	}
	r.records[code] = &record{invite: inv, revokeKeyHash: sha256.Sum256(revokeKey[:])}
	return Issued{Invite: cloneInvite(inv), RevokeKey: revokeKey}, nil
}

// Resolve は招待コードから招待情報を返す。
// 登録がなければ ErrNotFound、期限切れなら ErrExpired、失効済みなら ErrRevoked を返す。
func (r *Registry) Resolve(code Code) (Invite, error) {
	r.mu.Lock()
	defer r.mu.Unlock()
	rec, ok := r.records[code]
	if !ok {
		return Invite{}, ErrNotFound
	}
	if rec.revoked {
		return Invite{}, ErrRevoked
	}
	if !r.now().Before(rec.invite.ExpiresAt) {
		return Invite{}, ErrExpired
	}
	return cloneInvite(rec.invite), nil
}

// Revoke は招待を失効させる。revokeKey は Register が返した鍵。
// 登録がなければ ErrNotFound、鍵が違えば ErrForbidden を返す。失効済みや期限切れの招待に対しては何もせず成功する。
func (r *Registry) Revoke(code Code, revokeKey [RevokeKeyLen]byte) error {
	r.mu.Lock()
	defer r.mu.Unlock()
	rec, ok := r.records[code]
	if !ok {
		return ErrNotFound
	}
	// 鍵の照合の所要時間から鍵を推測されないように、定数時間で比較する。
	hash := sha256.Sum256(revokeKey[:])
	if subtle.ConstantTimeCompare(hash[:], rec.revokeKeyHash[:]) != 1 {
		return ErrForbidden
	}
	if rec.revoked {
		return nil
	}
	now := r.now()
	rec.revoked = true
	if now.Before(rec.invite.ExpiresAt) {
		rec.finishedAt = now
	} else {
		rec.finishedAt = rec.invite.ExpiresAt
	}
	// 失効した招待のトークンはもう渡さないので、メモリからも消しておく。
	rec.invite.Token = nil
	return nil
}

// Sweep は期限切れか失効から tombstoneRetention を過ぎた記録を消す。定期的に呼び出す。
func (r *Registry) Sweep() {
	r.mu.Lock()
	defer r.mu.Unlock()
	now := r.now()
	for code, rec := range r.records {
		finished := rec.invite.ExpiresAt
		if rec.revoked {
			finished = rec.finishedAt
		}
		if now.Sub(finished) >= tombstoneRetention {
			delete(r.records, code)
		}
	}
}

// uniqueCodeLocked は既存の招待と重ならない招待コードを生成する。r.mu を保持して呼び出す。
func (r *Registry) uniqueCodeLocked() (Code, error) {
	for range maxCodeAttempts {
		code, err := newCode(r.rand)
		if err != nil {
			return "", err
		}
		if _, exists := r.records[code]; !exists {
			return code, nil
		}
	}
	return "", fmt.Errorf("generate invite code: %d collisions in a row", maxCodeAttempts)
}

// cloneInvite は呼び出し側がトークンのスライスを書き換えても保存内容に影響しないように複製する。
func cloneInvite(inv Invite) Invite {
	inv.Token = append([]byte(nil), inv.Token...)
	return inv
}
