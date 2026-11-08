// SPDX-License-Identifier: AGPL-3.0-only

// 招待 API のハンドラーと JSON の DTO。
//
// 招待コードと招待トークンが URL のパスやクエリに入ると、前段のプロキシなどのアクセスログに残る。
// それを避けるため、解決と失効も含めてすべて POST の本文で受け渡す（docs/adr/0006-invite-url-scheme.md と同じ考え方）。
// バイト列は base64url（パディングなし）で表す。

package httpapi

import (
	"encoding/base64"
	"encoding/json"
	"errors"
	"net/http"
	"time"

	"github.com/YmSaki/vocroz/infra/rendezvous/internal/invite"
)

// maxRequestBody はリクエスト本文の上限。正しいリクエストは 1 KiB に収まる。
const maxRequestBody = 4 << 10

// エラーコード。子機と親機はこのコードからローカライズ済みのメッセージを表示する。
// INVITE_EXPIRED と INVITE_REVOKED は子機の FAILED の原因コードと同じ名前にしている。
const (
	errInvalidRequest = "INVALID_REQUEST"
	errInvalidCode    = "INVALID_CODE"
	errInviteNotFound = "INVITE_NOT_FOUND"
	errInviteExpired  = "INVITE_EXPIRED"
	errInviteRevoked  = "INVITE_REVOKED"
	errForbidden      = "FORBIDDEN"
	errCapacity       = "CAPACITY_EXCEEDED"
	errInternal       = "INTERNAL"
)

// registerRequest は POST /v1/invites の本文。親機が送る。
type registerRequest struct {
	HostPublicKey string `json:"host_public_key"`
	InviteToken   string `json:"invite_token"`
	RendezvousID  string `json:"rendezvous_id"`
	// TTLSeconds は有効期限（秒）。省略か 0 なら上限の 1800 秒。上限を超える値は上限に切り詰める。
	TTLSeconds int64 `json:"ttl_seconds,omitempty"`
}

// registerResponse は POST /v1/invites の応答。
type registerResponse struct {
	// Code は正規化した招待コード（ハイフンなし）。
	Code string `json:"code"`
	// DisplayCode は人に見せる形（XXXXX-XXXXX）。
	DisplayCode string    `json:"display_code"`
	ExpiresAt   time.Time `json:"expires_at"`
	// RevokeKey は失効に使う鍵。親機だけが保持し、ログに出さない。
	RevokeKey string `json:"revoke_key"`
}

// resolveRequest は POST /v1/invites/resolve の本文。子機が送る。
type resolveRequest struct {
	Code string `json:"code"`
}

// resolveResponse は POST /v1/invites/resolve の応答。
type resolveResponse struct {
	Code          string    `json:"code"`
	HostPublicKey string    `json:"host_public_key"`
	InviteToken   string    `json:"invite_token"`
	RendezvousID  string    `json:"rendezvous_id"`
	ExpiresAt     time.Time `json:"expires_at"`
}

// revokeRequest は POST /v1/invites/revoke の本文。親機が送る。
type revokeRequest struct {
	Code      string `json:"code"`
	RevokeKey string `json:"revoke_key"`
}

// errorResponse はエラー時の本文。
type errorResponse struct {
	Error string `json:"error"`
}

var b64 = base64.RawURLEncoding

// inviteAPI は招待 API のハンドラー。
type inviteAPI struct {
	invites *invite.Registry
}

func (a *inviteAPI) handleRegister(w http.ResponseWriter, r *http.Request) {
	var req registerRequest
	if !decodeJSON(w, r, &req) {
		return
	}
	var reg invite.Registration
	token, err := b64.DecodeString(req.InviteToken)
	if err != nil || !decodeFixed(req.HostPublicKey, reg.HostPublicKey[:]) ||
		!decodeFixed(req.RendezvousID, reg.RendezvousID[:]) || req.TTLSeconds < 0 {
		writeError(w, http.StatusBadRequest, errInvalidRequest)
		return
	}
	reg.Token = token
	// 上限を超える値は invite が切り詰めるが、Duration への変換で桁あふれしないように先に上限でそろえる。
	reg.TTL = time.Duration(min(req.TTLSeconds, int64(invite.MaxTTL/time.Second))) * time.Second

	issued, err := a.invites.Register(reg)
	switch {
	case errors.Is(err, invite.ErrInvalidInvite):
		writeError(w, http.StatusBadRequest, errInvalidRequest)
		return
	case errors.Is(err, invite.ErrCapacity):
		writeError(w, http.StatusServiceUnavailable, errCapacity)
		return
	case err != nil:
		writeError(w, http.StatusInternalServerError, errInternal)
		return
	}
	writeJSON(w, http.StatusCreated, registerResponse{
		Code:        string(issued.Invite.Code),
		DisplayCode: issued.Invite.Code.String(),
		ExpiresAt:   wireTime(issued.Invite.ExpiresAt),
		RevokeKey:   b64.EncodeToString(issued.RevokeKey[:]),
	})
}

func (a *inviteAPI) handleResolve(w http.ResponseWriter, r *http.Request) {
	var req resolveRequest
	if !decodeJSON(w, r, &req) {
		return
	}
	code, err := invite.ParseCode(req.Code)
	if err != nil {
		writeError(w, http.StatusBadRequest, errInvalidCode)
		return
	}
	inv, err := a.invites.Resolve(code)
	if err != nil {
		writeInviteError(w, err)
		return
	}
	writeJSON(w, http.StatusOK, resolveResponse{
		Code:          string(inv.Code),
		HostPublicKey: b64.EncodeToString(inv.HostPublicKey[:]),
		InviteToken:   b64.EncodeToString(inv.Token),
		RendezvousID:  b64.EncodeToString(inv.RendezvousID[:]),
		ExpiresAt:     wireTime(inv.ExpiresAt),
	})
}

func (a *inviteAPI) handleRevoke(w http.ResponseWriter, r *http.Request) {
	var req revokeRequest
	if !decodeJSON(w, r, &req) {
		return
	}
	code, err := invite.ParseCode(req.Code)
	if err != nil {
		writeError(w, http.StatusBadRequest, errInvalidCode)
		return
	}
	var key [invite.RevokeKeyLen]byte
	if !decodeFixed(req.RevokeKey, key[:]) {
		writeError(w, http.StatusBadRequest, errInvalidRequest)
		return
	}
	if err := a.invites.Revoke(code, key); err != nil {
		writeInviteError(w, err)
		return
	}
	w.Header().Set("Cache-Control", "no-store")
	w.WriteHeader(http.StatusNoContent)
}

// writeInviteError は invite のエラーを HTTP の状態とエラーコードに変換する。
func writeInviteError(w http.ResponseWriter, err error) {
	switch {
	case errors.Is(err, invite.ErrNotFound):
		writeError(w, http.StatusNotFound, errInviteNotFound)
	case errors.Is(err, invite.ErrExpired):
		writeError(w, http.StatusGone, errInviteExpired)
	case errors.Is(err, invite.ErrRevoked):
		writeError(w, http.StatusGone, errInviteRevoked)
	case errors.Is(err, invite.ErrForbidden):
		writeError(w, http.StatusForbidden, errForbidden)
	default:
		writeError(w, http.StatusInternalServerError, errInternal)
	}
}

// decodeJSON は本文を v にデコードする。失敗したら 400 を書いて false を返す。
// 未知のフィールドは無視する。クライアントが先に新しいフィールドを送り始めても、古いサーバーで動くようにするため。
func decodeJSON(w http.ResponseWriter, r *http.Request, v any) bool {
	dec := json.NewDecoder(http.MaxBytesReader(w, r.Body, maxRequestBody))
	if err := dec.Decode(v); err != nil || dec.More() {
		writeError(w, http.StatusBadRequest, errInvalidRequest)
		return false
	}
	return true
}

// decodeFixed は base64url の文字列を、ちょうど len(dst) バイトとしてデコードする。
func decodeFixed(s string, dst []byte) bool {
	b, err := b64.DecodeString(s)
	if err != nil || len(b) != len(dst) {
		return false
	}
	copy(dst, b)
	return true
}

func writeError(w http.ResponseWriter, status int, code string) {
	writeJSON(w, status, errorResponse{Error: code})
}

// writeJSON は v を JSON で書く。招待トークンなどを含むので、どこにもキャッシュさせない。
func writeJSON(w http.ResponseWriter, status int, v any) {
	w.Header().Set("Content-Type", "application/json; charset=utf-8")
	w.Header().Set("Cache-Control", "no-store")
	w.WriteHeader(status)
	_ = json.NewEncoder(w).Encode(v)
}

// wireTime は時刻を JSON に載せる形（UTC、秒単位）にそろえる。
// クライアントは有効期限を秒より細かく扱わないので、表記を短くして比較しやすくする。
func wireTime(t time.Time) time.Time {
	return t.UTC().Truncate(time.Second)
}
