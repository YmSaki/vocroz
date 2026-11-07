// SPDX-License-Identifier: AGPL-3.0-only

// Command rendezvous は VOCROZ のランデブーサーバー。
//
// 最終的には 1 つの UDP ポートで STUN・招待解決・候補アドレス交換を、
// HTTPS で招待 API と製品サイトを提供する。音声は通さず、メディア鍵も持たない。
// 現在は P0 の雛形で、ヘルスチェックだけを提供する。
package main

import (
	"context"
	"errors"
	"flag"
	"log"
	"net/http"
	"os/signal"
	"syscall"
	"time"

	"github.com/YmSaki/vocroz/infra/rendezvous/internal/httpapi"
)

// shutdownTimeout は終了シグナル受信後、処理中のリクエストを待つ上限。
const shutdownTimeout = 5 * time.Second

func main() {
	httpAddr := flag.String("http", ":8080", "HTTP の待ち受けアドレス")
	flag.Parse()

	srv := &http.Server{
		Addr:              *httpAddr,
		Handler:           httpapi.NewMux(),
		ReadHeaderTimeout: 5 * time.Second,
	}

	ctx, stop := signal.NotifyContext(context.Background(), syscall.SIGINT, syscall.SIGTERM)
	defer stop()

	go func() {
		log.Printf("rendezvous %s listening on %s", httpapi.Version, *httpAddr)
		if err := srv.ListenAndServe(); err != nil && !errors.Is(err, http.ErrServerClosed) {
			log.Fatal(err)
		}
	}()

	<-ctx.Done()
	shutdownCtx, cancel := context.WithTimeout(context.Background(), shutdownTimeout)
	defer cancel()
	if err := srv.Shutdown(shutdownCtx); err != nil {
		log.Printf("shutdown: %v", err)
	}
}
