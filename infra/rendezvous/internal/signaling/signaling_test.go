// SPDX-License-Identifier: AGPL-3.0-only

package signaling

import (
	"errors"
	"net/netip"
	"testing"
	"time"
)

type fakeClock struct{ t time.Time }

func (c *fakeClock) now() time.Time          { return c.t }
func (c *fakeClock) advance(d time.Duration) { c.t = c.t.Add(d) }

func newTestTable() (*Table, *fakeClock) {
	clock := &fakeClock{t: time.Date(2026, 11, 7, 12, 0, 0, 0, time.UTC)}
	return NewTable(clock.now), clock
}

var (
	sessionID = ID{1}
	hostID    = ID{0xA}
	clientID  = ID{0xC}
	hostAddr  = netip.MustParseAddrPort("203.0.113.10:40000")
	hostLAN   = netip.MustParseAddrPort("192.168.1.10:50000")
	clientAdr = netip.MustParseAddrPort("198.51.100.20:41000")
)

func hostReg() Registration {
	return Registration{RendezvousID: sessionID, PeerID: hostID, Role: RoleHost, Observed: hostAddr, Local: []netip.AddrPort{hostLAN, hostAddr}}
}

func clientReg(peer ID, addr netip.AddrPort) Registration {
	return Registration{RendezvousID: sessionID, PeerID: peer, Role: RoleClient, Observed: addr}
}

func TestTableIntroducesClientAndHostToEachOther(t *testing.T) {
	tbl, _ := newTestTable()
	if res, err := tbl.Register(hostReg()); err != nil || res.PeerPresent || len(res.Introductions) != 0 {
		t.Fatalf("host Register() = %+v, %v; want no peer", res, err)
	}

	res, err := tbl.Register(clientReg(clientID, clientAdr))
	if err != nil {
		t.Fatal(err)
	}
	if !res.PeerPresent || len(res.Introductions) != 2 {
		t.Fatalf("client Register() = %+v; want 2 introductions", res)
	}
	toHost, toClient := res.Introductions[0], res.Introductions[1]
	if toHost.To != hostAddr || toHost.PeerID != clientID || toHost.PeerRole != RoleClient ||
		len(toHost.Candidates) != 1 || toHost.Candidates[0] != clientAdr {
		t.Fatalf("introduction to host = %+v", toHost)
	}
	// 再帰アドレスを先頭に、重複した申告を除いて並ぶ。
	if toClient.To != clientAdr || toClient.PeerID != hostID || toClient.PeerRole != RoleHost ||
		len(toClient.Candidates) != 2 || toClient.Candidates[0] != hostAddr || toClient.Candidates[1] != hostLAN {
		t.Fatalf("introduction to client = %+v", toClient)
	}
}

func TestTableIntroducesWaitingClientsWhenHostRegisters(t *testing.T) {
	tbl, _ := newTestTable()
	if res, _ := tbl.Register(clientReg(clientID, clientAdr)); res.PeerPresent {
		t.Fatal("client sees a host before the host registered")
	}
	other := netip.MustParseAddrPort("198.51.100.30:42000")
	_, _ = tbl.Register(clientReg(ID{0xD}, other))

	res, err := tbl.Register(hostReg())
	if err != nil {
		t.Fatal(err)
	}
	if !res.PeerPresent || len(res.Introductions) != 4 {
		t.Fatalf("host Register() = %+v; want 4 introductions", res)
	}
}

func TestTableForgetsExpiredRegistrations(t *testing.T) {
	tbl, clock := newTestTable()
	_, _ = tbl.Register(hostReg())
	clock.advance(RegistrationTTL)

	res, _ := tbl.Register(clientReg(clientID, clientAdr))
	if res.PeerPresent {
		t.Fatal("expired host registration is still visible")
	}

	clock.advance(RegistrationTTL)
	tbl.Sweep()
	if len(tbl.sessions) != 0 {
		t.Fatalf("Sweep() left %d sessions", len(tbl.sessions))
	}
}

func TestTableLimitsClientsPerSession(t *testing.T) {
	tbl, _ := newTestTable()
	for i := range MaxClientsPerSession {
		if _, err := tbl.Register(clientReg(ID{byte(i), 1}, clientAdr)); err != nil {
			t.Fatal(err)
		}
	}
	if _, err := tbl.Register(clientReg(ID{0xFF, 0xFF}, clientAdr)); !errors.Is(err, ErrCapacity) {
		t.Fatalf("Register() error = %v, want ErrCapacity", err)
	}
	// 登録済みの子機の更新は上限に関係なく受け付ける。
	if _, err := tbl.Register(clientReg(ID{0, 1}, clientAdr)); err != nil {
		t.Fatalf("re-Register() error = %v", err)
	}
}
