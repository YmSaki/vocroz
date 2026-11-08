// SPDX-License-Identifier: AGPL-3.0-only

package udpapi

import (
	"net"
	"net/netip"
	"testing"
	"time"

	"github.com/YmSaki/vocroz/infra/rendezvous/internal/signaling"
)

// testClient は親機か子機の代わりに UDP でサーバーと話す。
type testClient struct {
	t    *testing.T
	conn *net.UDPConn
}

func newTestClient(t *testing.T) *testClient {
	t.Helper()
	conn, err := net.ListenUDP("udp4", &net.UDPAddr{IP: net.IPv4(127, 0, 0, 1)})
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { _ = conn.Close() })
	return &testClient{t: t, conn: conn}
}

func (c *testClient) addr() netip.AddrPort { return c.conn.LocalAddr().(*net.UDPAddr).AddrPort() }

func (c *testClient) send(to netip.AddrPort, b []byte) {
	c.t.Helper()
	if _, err := c.conn.WriteToUDPAddrPort(b, to); err != nil {
		c.t.Fatal(err)
	}
}

func (c *testClient) recv() []byte {
	c.t.Helper()
	_ = c.conn.SetReadDeadline(time.Now().Add(2 * time.Second))
	buf := make([]byte, maxDatagram)
	n, _, err := c.conn.ReadFromUDPAddrPort(buf)
	if err != nil {
		c.t.Fatal(err)
	}
	return buf[:n]
}

func startServer(t *testing.T) netip.AddrPort {
	t.Helper()
	conn, err := net.ListenUDP("udp4", &net.UDPAddr{IP: net.IPv4(127, 0, 0, 1)})
	if err != nil {
		t.Fatal(err)
	}
	done := make(chan error, 1)
	go func() { done <- Serve(conn, NewHandler(signaling.NewTable(time.Now))) }()
	t.Cleanup(func() {
		_ = conn.Close()
		if err := <-done; err != nil {
			t.Errorf("Serve() error = %v", err)
		}
	})
	return conn.LocalAddr().(*net.UDPAddr).AddrPort()
}

func TestServerExchangesCandidatesBetweenHostAndClient(t *testing.T) {
	server := startServer(t)
	host, client := newTestClient(t), newTestClient(t)
	rendezvousID := signaling.ID{0x42}
	hostLAN := netip.MustParseAddrPort("192.168.1.10:50000")

	host.send(server, Register{TxID: TxID{1}, RendezvousID: rendezvousID, PeerID: signaling.ID{0xA},
		Role: signaling.RoleHost, Candidates: []netip.AddrPort{hostLAN}}.Append(nil))
	ack, err := ParseRegistered(host.recv())
	if err != nil || ack.Status != StatusOK || ack.PeerPresent || ack.Reflexive != host.addr() {
		t.Fatalf("host REGISTERED = %+v, %v", ack, err)
	}

	client.send(server, Register{TxID: TxID{2}, RendezvousID: rendezvousID, PeerID: signaling.ID{0xC},
		Role: signaling.RoleClient}.Append(nil))
	ack, err = ParseRegistered(client.recv())
	if err != nil || !ack.PeerPresent || ack.TxID != (TxID{2}) {
		t.Fatalf("client REGISTERED = %+v, %v", ack, err)
	}

	toClient, err := ParsePunch(client.recv())
	if err != nil {
		t.Fatal(err)
	}
	if toClient.TxID != (TxID{2}) || toClient.PeerRole != signaling.RoleHost ||
		len(toClient.Candidates) != 2 || toClient.Candidates[0] != host.addr() || toClient.Candidates[1] != hostLAN {
		t.Fatalf("PUNCH to client = %+v", toClient)
	}

	toHost, err := ParsePunch(host.recv())
	if err != nil {
		t.Fatal(err)
	}
	if toHost.TxID != (TxID{}) || toHost.PeerID != (signaling.ID{0xC}) || toHost.PeerRole != signaling.RoleClient ||
		len(toHost.Candidates) != 1 || toHost.Candidates[0] != client.addr() {
		t.Fatalf("PUNCH to host = %+v", toHost)
	}
}

func TestServerAnswersStunBindingOnSamePort(t *testing.T) {
	server := startServer(t)
	c := newTestClient(t)
	req := []byte{0x00, 0x01, 0x00, 0x00, 0x21, 0x12, 0xA4, 0x42, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12}

	c.send(server, req)
	resp := c.recv()
	if resp[0] != 0x01 || resp[1] != 0x01 {
		t.Fatalf("response type = %x, want Binding success", resp[:2])
	}
}

func TestHandlerIgnoresUnknownPackets(t *testing.T) {
	h := NewHandler(signaling.NewTable(time.Now))
	from := netip.MustParseAddrPort("192.0.2.1:1000")
	for _, pkt := range [][]byte{nil, []byte("GET / HTTP/1.1\r\n"), []byte("VRZ\x01\x01short")} {
		if out := h.Handle(pkt, from); len(out) != 0 {
			t.Fatalf("Handle(%q) = %v, want nothing", pkt, out)
		}
	}
}

func TestHandlerReportsCapacityToRegistrant(t *testing.T) {
	h := NewHandler(signaling.NewTable(time.Now))
	from := netip.MustParseAddrPort("192.0.2.1:1000")
	for i := range signaling.MaxClientsPerSession {
		h.Handle(Register{RendezvousID: signaling.ID{1}, PeerID: signaling.ID{byte(i), 1}, Role: signaling.RoleClient}.Append(nil), from)
	}
	out := h.Handle(Register{RendezvousID: signaling.ID{1}, PeerID: signaling.ID{0xFF}, Role: signaling.RoleClient}.Append(nil), from)
	if len(out) != 1 {
		t.Fatalf("Handle() = %d datagrams, want 1", len(out))
	}
	if ack, err := ParseRegistered(out[0].Payload); err != nil || ack.Status != StatusCapacity {
		t.Fatalf("REGISTERED = %+v, %v; want capacity", ack, err)
	}
}
