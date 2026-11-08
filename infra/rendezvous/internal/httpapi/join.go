// SPDX-License-Identifier: AGPL-3.0-only

// 招待 URL（https://<サーバー>/join#<招待コード>）の着地ページ。
//
// 招待コードは URL のフラグメントにあり、ブラウザはサーバーに送らない。ページの JavaScript が
// フラグメントから「VOCROZ で開く」リンク（vocroz://join?code=...）を組み立てる。
// サーバーはコードを知らないまま、同じ静的ページを返すだけである（docs/adr/0006-invite-url-scheme.md）。

package httpapi

import (
	"bytes"
	"embed"
	"html/template"
	"io/fs"
	"net/http"
)

//go:embed static
var staticFiles embed.FS

// joinPage は /join のページと、そのスクリプトとスタイルを配信する。
type joinPage struct {
	html []byte
}

// newJoinPage はダウンロードリンクを埋め込んだページを組み立てる。
// ページはリクエストごとに変わらないので、起動時に 1 度だけ描画する。
func newJoinPage(downloadURL string) *joinPage {
	tmpl := template.Must(template.ParseFS(staticFiles, "static/join.html"))
	var buf bytes.Buffer
	if err := tmpl.Execute(&buf, struct{ DownloadURL string }{downloadURL}); err != nil {
		// テンプレートは埋め込みで固定なので、ここで失敗するのはビルドの誤り。
		panic(err)
	}
	return &joinPage{html: buf.Bytes()}
}

func (p *joinPage) handlePage(w http.ResponseWriter, _ *http.Request) {
	setPageSecurityHeaders(w)
	w.Header().Set("Content-Type", "text/html; charset=utf-8")
	_, _ = w.Write(p.html)
}

// assets は /join/join.js と /join/join.css を配信するハンドラーを返す。
func (p *joinPage) assets() http.Handler {
	sub, err := fs.Sub(staticFiles, "static")
	if err != nil {
		panic(err)
	}
	files := http.StripPrefix("/join/", http.FileServer(http.FS(sub)))
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		switch r.URL.Path {
		case "/join/join.js", "/join/join.css":
			setPageSecurityHeaders(w)
			files.ServeHTTP(w, r)
		default:
			http.NotFound(w, r)
		}
	})
}

// setPageSecurityHeaders はページに共通のセキュリティヘッダーを付ける。
// 外部のスクリプトやフレームへの埋め込みを禁じ、リファラーで招待 URL が外部に漏れないようにする。
func setPageSecurityHeaders(w http.ResponseWriter) {
	h := w.Header()
	h.Set("Content-Security-Policy", "default-src 'none'; script-src 'self'; style-src 'self'; frame-ancestors 'none'; base-uri 'none'; form-action 'none'")
	h.Set("Referrer-Policy", "no-referrer")
	h.Set("X-Content-Type-Options", "nosniff")
}
