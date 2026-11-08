// SPDX-License-Identifier: AGPL-3.0-only

// Command punchprobe は、ランデブーサーバー越しの UDP ホールパンチの成否を確かめる試験用のピア。
// ネットワーク名前空間で NAT を模した試験（tests/nat/run-nat-matrix.sh）が、親機側と子機側で 1 つずつ起動する。
// 結果を標準出力の 1 行（RESULT <結果> reflexive=<サーバーが見た自分> via=<相手の送信元>）で返し、
// CONNECTED なら終了コード 0、それ以外は 1 で終わる。製品には含めない。
//
// 使い方:
//
//	punchprobe -server 198.51.100.1:3478 -rendezvous 000102030405060708090a0b0c0d0e0f \
//	           -role client -local 10.0.1.2:40000 -timeout 5s
package main

import (
	"context"
	"encoding/hex"
	"errors"
	"flag"
	"fmt"
	"net"
	"net/netip"
	"os"
	"time"

	"github.com/YmSaki/vocroz/infra/rendezvous/internal/punchprobe"
	"github.com/YmSaki/vocroz/infra/rendezvous/internal/signaling"
)

func main() {
	code, err := run(os.Args[1:])
	if err != nil {
		fmt.Fprintln(os.Stderr, "punchprobe:", err)
	}
	os.Exit(code)
}

func run(args []string) (int, error) {
	fs := flag.NewFlagSet("punchprobe", flag.ContinueOnError)
	server := fs.String("server", "", "ランデブーサーバーの UDP のアドレス（ip:port）")
	rendezvous := fs.String("rendezvous", "", "ランデブー ID（16 バイトの 16 進数）")
	role := fs.String("role", "", "host か client")
	local := fs.String("local", "0.0.0.0:0", "待ち受けるアドレス。REGISTER のローカル候補にも使う")
	timeout := fs.Duration("timeout", 5*time.Second, "試行の上限")
	if err := fs.Parse(args); err != nil {
		return 2, err
	}
	cfg, laddr, err := parseConfig(*server, *rendezvous, *role, *local)
	if err != nil {
		return 2, err
	}
	conn, err := net.ListenUDP("udp4", net.UDPAddrFromAddrPort(laddr))
	if err != nil {
		return 2, err
	}
	defer conn.Close()
	ctx, cancel := context.WithTimeout(context.Background(), *timeout)
	defer cancel()
	res, err := punchprobe.Run(ctx, conn, cfg)
	if err != nil {
		return 2, err
	}
	fmt.Printf("RESULT %s reflexive=%s via=%s peer_candidates=%v\n", res.Outcome, res.Reflexive, res.Via, res.PeerCandidates)
	if res.Outcome != punchprobe.OutcomeConnected {
		return 1, nil
	}
	return 0, nil
}

func parseConfig(server, rendezvous, role, local string) (punchprobe.Config, netip.AddrPort, error) {
	var cfg punchprobe.Config
	addr, err := netip.ParseAddrPort(server)
	if err != nil {
		return cfg, netip.AddrPort{}, fmt.Errorf("-server: %w", err)
	}
	cfg.Server = addr
	id, err := hex.DecodeString(rendezvous)
	if err != nil || len(id) != signaling.IDLen {
		return cfg, netip.AddrPort{}, errors.New("-rendezvous must be 16 bytes in hex")
	}
	copy(cfg.RendezvousID[:], id)
	switch role {
	case "host":
		cfg.Role = signaling.RoleHost
	case "client":
		cfg.Role = signaling.RoleClient
	default:
		return cfg, netip.AddrPort{}, errors.New("-role must be host or client")
	}
	laddr, err := netip.ParseAddrPort(local)
	if err != nil {
		return cfg, netip.AddrPort{}, fmt.Errorf("-local: %w", err)
	}
	// 未指定アドレスはサーバーが候補から除くので、具体的なアドレスのときだけローカル候補として載せる。
	if !laddr.Addr().IsUnspecified() && laddr.Port() != 0 {
		cfg.Local = []netip.AddrPort{laddr}
	}
	return cfg, laddr, nil
}
