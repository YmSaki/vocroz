// SPDX-License-Identifier: AGPL-3.0-only

// Package httpapi はランデブーサーバーの HTTP(S) 側のハンドラーを提供する。
// 招待解決 API（P3）と製品サイトもこのパッケージに追加する。
package httpapi

import "net/http"

// Version はサーバーのバージョン。デスクトップアプリと同じ番号で管理する。
const Version = "0.0.1"

// NewMux はすべての HTTP ルートを登録したハンドラーを返す。
func NewMux() *http.ServeMux {
	mux := http.NewServeMux()
	mux.HandleFunc("GET /healthz", handleHealthz)
	return mux
}

// handleHealthz は死活監視用に "ok <バージョン>" を返す。
func handleHealthz(w http.ResponseWriter, _ *http.Request) {
	w.Header().Set("Content-Type", "text/plain; charset=utf-8")
	_, _ = w.Write([]byte("ok " + Version + "\n"))
}
