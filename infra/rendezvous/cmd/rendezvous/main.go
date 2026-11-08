// SPDX-License-Identifier: AGPL-3.0-only

// Command rendezvous は VOCROZ のランデブーサーバー。エントリポイント兼コンポジションルート。
//
// 1 つの UDP ポートで STUN Binding と候補アドレスの交換を、HTTP(S) で招待 API と /join ページを提供する。
// 音声は通さず、メディア鍵も持たない。サーバーが止まっても確立済みのセッションは続く。
//
// 特定のホスティングに依存しないように、設定はコマンドライン引数だけで受け取り、状態はメモリにだけ置く。
// TLS は -tls-cert と -tls-key で自分で終端するか、前段のリバースプロキシに任せる。
package main

import (
	"context"
	"crypto/rand"
	"errors"
	"flag"
	"fmt"
	"log"
	"net"
	"net/http"
	"net/url"
	"os"
	"os/signal"
	"syscall"
	"time"

	"github.com/YmSaki/vocroz/infra/rendezvous/internal/httpapi"
	"github.com/YmSaki/vocroz/infra/rendezvous/internal/invite"
	"github.com/YmSaki/vocroz/infra/rendezvous/internal/signaling"
	"github.com/YmSaki/vocroz/infra/rendezvous/internal/udpapi"
)

const (
	// shutdownTimeout は終了シグナル受信後、処理中のリクエストを待つ上限。
	shutdownTimeout = 5 * time.Second

	// sweepInterval は期限切れの招待と登録を消す間隔。
	// 登録の保持時間（30 秒）と同じ程度にして、メモリに残る期限切れの登録を少なくする。
	sweepInterval = 30 * time.Second

	// defaultDownloadURL は /join ページのダウンロードリンクの既定値。
	defaultDownloadURL = "https://github.com/YmSaki/vocroz/releases/latest"
)

// options はコマンドライン引数。
type options struct {
	httpAddr    string
	udpAddr     string
	tlsCert     string
	tlsKey      string
	downloadURL string
}

func main() {
	opts, err := parseOptions(os.Args[1:])
	if err != nil {
		fmt.Fprintln(os.Stderr, err)
		os.Exit(2)
	}
	if err := run(opts); err != nil {
		log.Fatal(err)
	}
}

// parseOptions は引数を検証済みの options に変換する。
func parseOptions(args []string) (options, error) {
	var o options
	fs := flag.NewFlagSet("rendezvous", flag.ContinueOnError)
	fs.StringVar(&o.httpAddr, "http", ":8080", "HTTP(S) の待ち受けアドレス")
	fs.StringVar(&o.udpAddr, "udp", ":3478", "STUN とランデブーメッセージを受ける UDP のアドレス（3478 は STUN の標準ポート）")
	fs.StringVar(&o.tlsCert, "tls-cert", "", "TLS 証明書（PEM）のパス。指定すると HTTPS で待ち受ける")
	fs.StringVar(&o.tlsKey, "tls-key", "", "TLS 秘密鍵（PEM）のパス")
	fs.StringVar(&o.downloadURL, "download-url", defaultDownloadURL, "/join ページのダウンロードリンク")
	if err := fs.Parse(args); err != nil {
		return options{}, err
	}
	if fs.NArg() > 0 {
		return options{}, fmt.Errorf("unexpected arguments: %v", fs.Args())
	}
	if (o.tlsCert == "") != (o.tlsKey == "") {
		return options{}, errors.New("-tls-cert and -tls-key must be given together")
	}
	if u, err := url.Parse(o.downloadURL); err != nil || (u.Scheme != "https" && u.Scheme != "http") || u.Host == "" {
		return options{}, fmt.Errorf("-download-url must be an absolute http(s) URL: %q", o.downloadURL)
	}
	return o, nil
}

// run はサーバーを組み立てて起動し、終了シグナルを受けるまで動かす。
func run(o options) error {
	invites := invite.NewRegistry(time.Now, rand.Reader)
	table := signaling.NewTable(time.Now)

	udpAddr, err := net.ResolveUDPAddr("udp", o.udpAddr)
	if err != nil {
		return fmt.Errorf("resolve -udp: %w", err)
	}
	udpConn, err := net.ListenUDP("udp", udpAddr)
	if err != nil {
		return fmt.Errorf("listen udp: %w", err)
	}

	srv := &http.Server{
		Addr:              o.httpAddr,
		Handler:           httpapi.NewMux(httpapi.Config{Invites: invites, DownloadURL: o.downloadURL}),
		ReadHeaderTimeout: 5 * time.Second,
		ReadTimeout:       10 * time.Second,
		WriteTimeout:      10 * time.Second,
		IdleTimeout:       60 * time.Second,
	}

	ctx, stop := signal.NotifyContext(context.Background(), syscall.SIGINT, syscall.SIGTERM)
	defer stop()

	errc := make(chan error, 2)
	go func() {
		log.Printf("rendezvous %s: udp on %s", httpapi.Version, udpConn.LocalAddr())
		errc <- udpapi.Serve(udpConn, udpapi.NewHandler(table))
	}()
	go func() {
		var err error
		if o.tlsCert != "" {
			log.Printf("rendezvous %s: https on %s", httpapi.Version, o.httpAddr)
			err = srv.ListenAndServeTLS(o.tlsCert, o.tlsKey)
		} else {
			log.Printf("rendezvous %s: http on %s", httpapi.Version, o.httpAddr)
			err = srv.ListenAndServe()
		}
		if errors.Is(err, http.ErrServerClosed) {
			err = nil
		}
		errc <- err
	}()
	go sweep(ctx, invites, table)

	var runErr error
	select {
	case <-ctx.Done():
	case runErr = <-errc:
	}

	shutdownCtx, cancel := context.WithTimeout(context.Background(), shutdownTimeout)
	defer cancel()
	if err := srv.Shutdown(shutdownCtx); err != nil {
		log.Printf("shutdown http: %v", err)
	}
	_ = udpConn.Close()
	return runErr
}

// sweep は ctx が終わるまで、期限切れの招待と登録を定期的に消す。
func sweep(ctx context.Context, invites *invite.Registry, table *signaling.Table) {
	ticker := time.NewTicker(sweepInterval)
	defer ticker.Stop()
	for {
		select {
		case <-ctx.Done():
			return
		case <-ticker.C:
			invites.Sweep()
			table.Sweep()
		}
	}
}
