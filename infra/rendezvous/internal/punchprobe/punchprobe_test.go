// SPDX-License-Identifier: AGPL-3.0-only

package punchprobe

import (
	"context"
	"net"
	"net/netip"
	"testing"
	"time"

	"github.com/YmSaki/vocroz/infra/rendezvous/internal/signaling"
	"github.com/YmSaki/vocroz/infra/rendezvous/internal/udpapi"
)

var testRendezvousID = signaling.ID{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16}

func listenLoopback(t *testing.T) *net.UDPConn {
	t.Helper()
	conn, err := net.ListenUDP("udp4", &net.UDPAddr{IP: net.IPv4(127, 0, 0, 1)})
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { _ = conn.Close() })
	return conn
}

func addrOf(conn *net.UDPConn) netip.AddrPort {
	return conn.LocalAddr().(*net.UDPAddr).AddrPort()
}

func startServer(t *testing.T) netip.AddrPort {
	t.Helper()
	conn := listenLoopback(t)
	go func() { _ = udpapi.Serve(conn, udpapi.NewHandler(signaling.NewTable(time.Now))) }()
	return addrOf(conn)
}

func run(t *testing.T, server netip.AddrPort, role signaling.Role, timeout time.Duration) <-chan Result {
	t.Helper()
	conn := listenLoopback(t)
	out := make(chan Result, 1)
	go func() {
		ctx, cancel := context.WithTimeout(context.Background(), timeout)
		defer cancel()
		res, err := Run(ctx, conn, Config{Server: server, RendezvousID: testRendezvousID, Role: role})
		if err != nil {
			t.Error(err)
		}
		out <- res
	}()
	return out
}

func TestBothPeersConnectThroughRendezvous(t *testing.T) {
	server := startServer(t)
	host := run(t, server, signaling.RoleHost, 5*time.Second)
	client := run(t, server, signaling.RoleClient, 5*time.Second)
	for name, ch := range map[string]<-chan Result{"host": host, "client": client} {
		if res := <-ch; res.Outcome != OutcomeConnected {
			t.Errorf("%s: outcome %s, want %s", name, res.Outcome, OutcomeConnected)
		}
	}
}

func TestNoServerIsRendezvousUnreachable(t *testing.T) {
	// 誰も待ち受けていないポート。
	silent := listenLoopback(t)
	server := addrOf(silent)
	_ = silent.Close()
	if res := <-run(t, server, signaling.RoleClient, 700*time.Millisecond); res.Outcome != OutcomeRendezvousUnreachable {
		t.Errorf("outcome %s, want %s", res.Outcome, OutcomeRendezvousUnreachable)
	}
}

func TestMissingPeerIsPeerNotFound(t *testing.T) {
	server := startServer(t)
	if res := <-run(t, server, signaling.RoleClient, 700*time.Millisecond); res.Outcome != OutcomePeerNotFound {
		t.Errorf("outcome %s, want %s", res.Outcome, OutcomePeerNotFound)
	}
}

func TestSilentPeerIsUnreachable(t *testing.T) {
	server := startServer(t)
	// 登録だけして試験パケットに答えない相手（NAT が相手からのパケットを通さない場合と同じに見える）。
	silent := listenLoopback(t)
	reg := udpapi.Register{RendezvousID: testRendezvousID, PeerID: signaling.ID{9}, Role: signaling.RoleHost}
	if _, err := silent.WriteToUDPAddrPort(reg.Append(nil), server); err != nil {
		t.Fatal(err)
	}
	res := <-run(t, server, signaling.RoleClient, time.Second)
	if res.Outcome != OutcomeUnreachable {
		t.Errorf("outcome %s, want %s", res.Outcome, OutcomeUnreachable)
	}
	if len(res.PeerCandidates) == 0 || res.PeerCandidates[0] != addrOf(silent) {
		t.Errorf("peer candidates %v, want %v first", res.PeerCandidates, addrOf(silent))
	}
}
