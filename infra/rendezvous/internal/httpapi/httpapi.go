// SPDX-License-Identifier: AGPL-3.0-only

// Package httpapi はランデブーサーバーの HTTP(S) 側の外部インターフェース。
// 招待の登録・解決・失効の API、招待 URL の着地ページ（/join）、死活監視を提供する。
// JSON のスキーマはこのパッケージが正本で、親機と子機のクライアント実装はこれに合わせる（AGENTS.md 4.4）。
//
// TLS はこのパッケージでは扱わない。cmd/rendezvous が自分で終端するか、前段のリバースプロキシや
// ホスティングのロードバランサーが終端する。特定のホスティングに依存しないようにするため。
package httpapi

import (
	"net/http"

	"github.com/YmSaki/vocroz/infra/rendezvous/internal/invite"
)

// Version はサーバーのバージョン。デスクトップアプリと同じ番号で管理する。
const Version = "0.0.1"

// Config は HTTP ハンドラーの依存と設定。
type Config struct {
	// Invites は招待の保存先。
	Invites *invite.Registry
	// DownloadURL は /join ページの「ダウンロード」リンクの行き先。
	DownloadURL string
}

// NewMux はすべての HTTP ルートを登録したハンドラーを返す。
func NewMux(cfg Config) *http.ServeMux {
	mux := http.NewServeMux()
	mux.HandleFunc("GET /healthz", handleHealthz)

	api := &inviteAPI{invites: cfg.Invites}
	mux.HandleFunc("POST /v1/invites", api.handleRegister)
	mux.HandleFunc("POST /v1/invites/resolve", api.handleResolve)
	mux.HandleFunc("POST /v1/invites/revoke", api.handleRevoke)

	join := newJoinPage(cfg.DownloadURL)
	mux.HandleFunc("GET /join", join.handlePage)
	mux.Handle("GET /join/", join.assets())
	return mux
}

// handleHealthz は死活監視用に "ok <バージョン>" を返す。
func handleHealthz(w http.ResponseWriter, _ *http.Request) {
	w.Header().Set("Content-Type", "text/plain; charset=utf-8")
	_, _ = w.Write([]byte("ok " + Version + "\n"))
}
